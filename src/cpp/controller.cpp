//
// (c) 2026 Copyright, Real-Time Innovations, Inc. (RTI) All rights reserved.
//
// RTI grants Licensee a license to use, modify, compile, and create derivative
// works of the Software solely in combination with RTI Connext DDS. Licensee
// may redistribute copies of the Software provided that all such copies are
// subject to this License. The Software is provided "as is", with no warranty
// of any type, including any warranty for fitness for any purpose. RTI is
// under no obligation to maintain or support the Software. RTI shall not be
// liable for any incidental or consequential damages arising out of the use or
// inability to use the Software. For purposes of clarity, nothing in this
// License prevents Licensee from using alternate versions of DDS, provided
// that Licensee may not combine or link such alternate versions of DDS with
// the Software.
//

#include <dds/dds.hpp>
#include <rti/rti.hpp>

#include "control/Control.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

std::atomic_bool stop_requested{false};

void signal_handler(int)
{
    stop_requested = true;
}

class Controller {
public:
    Controller(unsigned int priority = 0, int domain_id = 0)
    {
        dds::core::QosProvider qos_provider = dds::core::QosProvider::Default();

        // Participant Factory Qos defines:
        //  - entity enabling (explicitly or automatically on creation)
        //  - logging
        //  - monitoring
        dds::domain::DomainParticipant::participant_factory_qos(
            qos_provider->participant_factory_qos("Qos::Default"));

        participant_ = dds::domain::DomainParticipant(
            domain_id, qos_provider.participant_qos("Qos::Default"));

        command_writer_ = dds::pub::DataWriter(
            dds::topic::Topic<control::Command>(participant_, "control/Command"),
            qos_provider.datawriter_qos("Qos::Command"));
        set_priority(priority);

        state_reader_ = dds::sub::DataReader(
            dds::topic::Topic<control::State>(participant_, "control/State"),
            qos_provider.datareader_qos("Qos::State"));

        position_reader_ = dds::sub::DataReader(
            dds::topic::Topic<control::Position>(participant_, "control/Position"),
            qos_provider.datareader_qos("Qos::Stream"));

        alert_reader_ = dds::sub::DataReader(
            dds::topic::Topic<control::Alert>(participant_, "control/Alert"),
            qos_provider.datareader_qos("Qos::Event"));
        
        // Async alert consumption via SampleProcessor
        processor_.attach_reader(
                alert_reader_,
                [this](const rti::sub::LoanedSample<control::Alert>& sample) {
                    if (!sample.info().valid()) {
                        return;
                    }
                    const auto& data = sample.data();
                    std::cout << "\n  [alert] " << data.source_id
                              << ": level=" << static_cast<int>(data.level);
                    if (data.message.has_value()) {
                        std::cout << " msg=\"" << *data.message << "\"";
                    }
                    std::cout << std::endl;
                });
    }

    void run()
    {
        
        std::cout << "[controller] starting..." << std::endl;
        participant_.enable();
        std::cout << "\n[controller] Ready. Commands: list, positions, send, priority, help, quit\n"
                  << std::endl;

        std::string line;
        while (!stop_requested) {
            std::cout << "[controller]> " << std::flush;
            if (!std::getline(std::cin, line)) {
                break;
            }
            if (line.empty()) {
                continue;
            }

            std::istringstream iss(line);
            std::string command;
            iss >> command;

            if (command == "quit" || command == "exit") {
                break;
            } else if (command == "help") {
                print_help();
            } else if (command == "list") {
                do_list();
            } else if (command == "positions") {
                do_positions();
            } else if (command == "send") {
                do_send(iss);
            } else if (command == "priority") {
                do_priority(iss);
            } else {
                std::cout << "  Unknown command '" << command
                          << "'. Type 'help' for usage." << std::endl;
            }
        }
    }

private:
    void print_help()
    {
        std::cout << "\nCommands:\n"
                  << "  list                  - Show discovered targets (active marked with *)\n"
                  << "  positions             - Show current positions of all targets\n"
                  << "  send <target|all> <action> [x y z]\n"
                  << "                        - Actions: start, stop, pause\n"
                  << "  priority <value>      - Set controller priority\n"
                  << "  help                  - Show this help\n"
                  << "  quit                  - Exit\n"
                  << std::endl;
    }

    void do_list()
    {
        auto samples = rti::sub::valid_data(state_reader_.read());
        bool found = false;
        for (const auto& sample : samples) {
            const auto& state = sample.data();
            std::string marker = state.mode == control::ModeType::ACTIVE ? "*" : " ";
            std::cout << "  [" << marker << "] " << state.source_id
                      << ": mode=" << state.mode
                      << std::endl;
            found = true;
        }
        if (!found) {
            std::cout << "  No targets discovered yet." << std::endl;
        }
    }

    void do_positions()
    {
        auto samples = rti::sub::valid_data(position_reader_.read());
        bool found = false;
        for (const auto& sample : samples) {
            const auto& pos = sample.data();
            std::cout << "  " << pos.source_id << ": ("
                      << pos.pose.x << ", "
                      << pos.pose.y << ", "
                      << pos.pose.z << ")" << std::endl;
            found = true;
        }
        if (!found) {
            std::cout << "  No position data available yet." << std::endl;
        }
    }

    void do_send(std::istringstream& iss)
    {
        std::string target_str, action_str;
        iss >> target_str >> action_str;

        if (target_str.empty() || action_str.empty()) {
            std::cout << "  Usage: send <target|all> <action> [x y z]" << std::endl;
            return;
        }

        control::ActionType action;
        if (!parse_action(action_str, action)) {
            std::cout << "  Error: Unknown action '" << action_str
                      << "'. Use: start, stop, pause" << std::endl;
            return;
        }

        // Parse optional pose
        rti::core::optional<control::Pose> pose;
        float x, y, z;
        if (iss >> x >> y >> z) {
            pose = control::Pose{x, y, z};
        }

        if (target_str == "all") {
            send_command(control::Uid(control::ALL_TARGETS), action, pose);
        } else {
            // Validate target exists
            auto samples = rti::sub::valid_data(state_reader_.read());
            bool found = false;
            for (const auto& sample : samples) {
                if (sample.data().source_id == target_str) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                std::cout << "  Error: '" << target_str
                          << "' not discovered. Use 'list' to see targets."
                          << std::endl;
                return;
            }
            send_command(control::Uid(target_str), action, pose);
        }
    }

    void do_priority(std::istringstream& iss)
    {
        unsigned int priority;
        if (!(iss >> priority)) {
            std::cout << "  Error: Priority must be a non-negative integer" << std::endl;
            return;
        }
        set_priority(priority);
        std::cout << "  Priority set to " << priority << std::endl;
    }

    bool parse_action(const std::string& str, control::ActionType& action)
    {
        if (str == "start") { action = control::ActionType::START; return true; }
        if (str == "stop") { action = control::ActionType::STOP; return true; }
        if (str == "pause") { action = control::ActionType::PAUSE; return true; }
        return false;
    }

    void send_command(const control::Uid& target_id, control::ActionType action, const rti::core::optional<control::Pose>& pose)
    {
        command_writer_.write(control::Command{target_id, action, pose});
        std::cout << "  -> sent " << action << " to "
                  << target_id << std::endl;
    }

    void set_priority(unsigned int priority)
    {
        auto qos = command_writer_.qos();
        command_writer_.qos(qos << dds::core::policy::OwnershipStrength(priority));
    }

    dds::domain::DomainParticipant participant_ = nullptr;
    dds::pub::DataWriter<control::Command> command_writer_ = nullptr;
    dds::sub::DataReader<control::State> state_reader_ = nullptr;
    dds::sub::DataReader<control::Position> position_reader_ = nullptr;
    dds::sub::DataReader<control::Alert> alert_reader_ = nullptr;

    rti::sub::SampleProcessor processor_;
};


int main(int argc, char* argv[])
{
    std::signal(SIGINT, signal_handler);

    unsigned int priority = 0;
    int domain_id = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--priority" && i + 1 < argc) {
            priority = std::stoul(argv[++i]);
        } else if (arg == "--domain" && i + 1 < argc) {
            domain_id = std::stoi(argv[++i]);
        } else {
            std::cerr << "Usage: " << argv[0]
                      << " [--priority <priority>] [--domain <id>]"
                      << std::endl;
            return 1;
        }
    }

    try {
        Controller controller(priority, domain_id);
        controller.run();
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
