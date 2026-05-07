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
#include <cmath>
#include <csignal>
#include <iostream>
#include <mutex>
#include <thread>

std::atomic_bool stop_requested{false};

void signal_handler(int)
{
    stop_requested = true;
}

class Target {
public:
    Target(control::Uid uid, int domain_id = 0)
            : uid_(std::move(uid))
    {
        position_.source_id = uid_;

        dds::core::QosProvider qos_provider = dds::core::QosProvider::Default();

        // Participant Factory Qos defines:
        //  - entity enabling (explicitly or automatically on creation)
        //  - logging
        //  - monitoring
        dds::domain::DomainParticipant::participant_factory_qos(
            qos_provider->participant_factory_qos("Qos::Default"));

        participant_ = dds::domain::DomainParticipant(
            domain_id, qos_provider.participant_qos("Qos::Default"));

        state_writer_ = dds::pub::DataWriter(
            dds::topic::Topic<control::State>(participant_, "control/State"),
            qos_provider.datawriter_qos("Qos::State"));

        position_writer_ = dds::pub::DataWriter(
            dds::topic::Topic<control::Position>(participant_, "control/Position"),
            qos_provider.datawriter_qos("Qos::Stream"));

        alert_writer_ = dds::pub::DataWriter(
            dds::topic::Topic<control::Alert>(participant_, "control/Alert"),
            qos_provider.datawriter_qos("Qos::Event"));

        std::ostringstream filter_expr;
        filter_expr << "target_id = %0 or target_id = '" << control::ALL_TARGETS << "'";
        command_reader_ = dds::sub::DataReader(
            dds::topic::ContentFilteredTopic<control::Command>(
                dds::topic::Topic<control::Command>(participant_, "control/Command"),
                "control::Command@" + uid_,
                dds::topic::Filter(filter_expr.str(), {"'" + uid_ + "'"})),
            qos_provider.datareader_qos("Qos::Command"));

        // Async command consumption via SampleProcessor
        processor_.attach_reader(
                command_reader_,
                [this](const rti::sub::LoanedSample<control::Command>& sample) {
                    if (!sample.info().valid()) {
                        return;
                    }
                    process_command(sample.data());
                });
    }

    void run()
    {
        std::cout << "[target " << uid_ << "] starting with initial position ("
                  << position_.pose.x << ", "
                  << position_.pose.y << ", "
                  << position_.pose.z << ")"
                  << std::endl;
        participant_.enable();
        state_writer_.write(control::State{uid_, mode_});  // Publish initial state
        unsigned int i = 0;
        while (!stop_requested && (mode_ == control::ModeType::ACTIVE || mode_ == control::ModeType::IDLE)) {
            // Periodically print position
            if (i++ % 5 == 0) {
                std::cout << "[target " << uid_ << "] position=("
                          << position_.pose.x << ", "
                          << position_.pose.y << ", "
                          << position_.pose.z << ")";
                if (arrived_) {
                    std::cout << " (arrived at dest)";
                } else {
                    std::cout << " dest=("
                              << target_pose_.x << ", "
                              << target_pose_.y << ", "
                              << target_pose_.z << ")";
                }
                std::cout << std::endl;
            }

            // Publish position
            position_writer_.write(position_);

            // Simulate movement toward target_pose (shared with callback)
            if (mode_ == control::ModeType::ACTIVE) {
                std::lock_guard<std::mutex> lock(mtx_);
                if (!arrived_) {
                    const float rate = 0.1f;
                    auto& pose = position_.pose;
                    pose.x = pose.x + (target_pose_.x - pose.x) * rate;
                    pose.y = pose.y + (target_pose_.y - pose.y) * rate;
                    pose.z = pose.z + (target_pose_.z - pose.z) * rate;

                    if (std::abs(pose.x - target_pose_.x) < 0.01f &&
                        std::abs(pose.y - target_pose_.y) < 0.01f &&
                        std::abs(pose.z - target_pose_.z) < 0.01f) {
                        
                        // Target has arrived at the target position
                        arrived_ = true;
                        pose.x = target_pose_.x;
                        pose.y = target_pose_.y;
                        pose.z = target_pose_.z;
                        position_writer_.write(position_); // Publish final position update

                        // Update state to IDLE upon arrival at target position
                        mode_ = control::ModeType::IDLE;
                        state_writer_.write(control::State{uid_, mode_});

                        // Publish an alert indicating arrival at target position
                        alert_writer_.write(control::Alert{uid_, control::AlertLevel::INFO, std::string("Arrived at target position")});
                    }
                }
            }
            
            // Simulate time delay between updates
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

private:
    void process_command(const control::Command& cmd)
    {
        if (cmd.target_id != uid_ && cmd.target_id != control::ALL_TARGETS) {
            std::cerr << "[target " << uid_ << "] Received command for target_id="
                      << cmd.target_id << ", expected " << uid_ << std::endl;
            return;
        }

        std::cout << "[target " << uid_ << "] received command action=" << cmd.action;
        if (cmd.target_pose.has_value()) {
            std::cout << " target_pose=("
                      << cmd.target_pose->x << ", "
                      << cmd.target_pose->y << ", "
                      << cmd.target_pose->z << ")";
        }
        std::cout << std::endl;

        std::lock_guard<std::mutex> lock(mtx_);
        if (cmd.action == control::ActionType::STOP) {
            alert_writer_.write(control::Alert{uid_, control::AlertLevel::WARNING, std::string("Stop command received")});

            mode_.store(control::ModeType::IDLE);
            control::State state{uid_, control::ModeType::IDLE};
            state_writer_.write(state);
            state_writer_.dispose_instance(state_writer_.lookup_instance(state));
            stop_requested = true;

        } else if (cmd.action == control::ActionType::START) {
            auto expected = control::ModeType::IDLE;
            if (mode_.compare_exchange_strong(expected, control::ModeType::ACTIVE)) {
                state_writer_.write(control::State{uid_, control::ModeType::ACTIVE});
            }
        } else if (cmd.action == control::ActionType::PAUSE) {
            auto expected = control::ModeType::ACTIVE;
            if (mode_.compare_exchange_strong(expected, control::ModeType::IDLE)) {
                state_writer_.write(control::State{uid_, control::ModeType::IDLE});
            }
        }

        if (cmd.target_pose.has_value()) {
            target_pose_ = *cmd.target_pose;
            arrived_ = false;
        }
    }

    control::Uid uid_;
    std::mutex mtx_;
    std::atomic<control::ModeType> mode_{control::ModeType::IDLE};
    control::Position position_;
    control::Pose target_pose_;
    bool arrived_{true};

    dds::domain::DomainParticipant participant_ = nullptr;
    dds::sub::DataReader<control::Command> command_reader_ = nullptr;
    dds::pub::DataWriter<control::State> state_writer_ = nullptr;
    dds::pub::DataWriter<control::Position> position_writer_ = nullptr;
    dds::pub::DataWriter<control::Alert> alert_writer_ = nullptr;

    rti::sub::SampleProcessor processor_;
};


int main(int argc, char* argv[])
{
    std::signal(SIGINT, signal_handler);

    control::Uid uid{"target-1"};
    int domain_id = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--uid" && i + 1 < argc) {
            uid = control::Uid(argv[++i]);
        } else if (arg == "--domain" && i + 1 < argc) {
            domain_id = std::stoi(argv[++i]);
        } else {
            std::cerr << "Usage: " << argv[0]
                      << " [--uid <target_id>] [--domain <id>]" << std::endl;
            return 1;
        }
    }

    try {
        Target target(uid, domain_id);
        target.run();
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
