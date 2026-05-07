#
# (c) 2026 Copyright, Real-Time Innovations, Inc. (RTI) All rights reserved.
#
# RTI grants Licensee a license to use, modify, compile, and create derivative
# works of the Software solely in combination with RTI Connext DDS. Licensee
# may redistribute copies of the Software provided that all such copies are
# subject to this License. The Software is provided "as is", with no warranty
# of any type, including any warranty for fitness for any purpose. RTI is
# under no obligation to maintain or support the Software. RTI shall not be
# liable for any incidental or consequential damages arising out of the use or
# inability to use the Software. For purposes of clarity, nothing in this
# License prevents Licensee from using alternate versions of DDS, provided
# that Licensee may not combine or link such alternate versions of DDS with
# the Software.
#

import argparse
import asyncio
import cmd
import shlex
import sys
from dataclasses import dataclass, field
from pathlib import Path

import rti.asyncio
import rti.connextdds as dds

sys.path.append(str(Path(__file__).resolve().parent.parent / "interface" / "control"))
from Control import control


@dataclass
class Controller(cmd.Cmd):
    """Interactive controller that discovers and commands targets via DDS."""

    priority: int = 0
    domain_id: int = 0

    intro = "Type 'help' for available commands.\n"

    ACTIONS = {
        "start": control.ActionType.START,
        "stop": control.ActionType.STOP,
        "pause": control.ActionType.PAUSE,
    }

    # DDS entities — set up in __post_init__
    _participant: dds.DomainParticipant = field(init=False, repr=False)
    _command_writer: dds.DataWriter = field(init=False, repr=False)
    _state_reader: dds.DataReader = field(init=False, repr=False)
    _position_reader: dds.DataReader = field(init=False, repr=False)
    _alert_reader: dds.DataReader = field(init=False, repr=False)

    def __post_init__(self):
        """Initialize cmd.Cmd and DDS entities."""
        cmd.Cmd.__init__(self)
        self.prompt = "[controller]> "

        qos_provider = dds.QosProvider.default

        # Participant Factory Qos defines:
        #  - entity enabling (explicitly or automatically on creation)
        #  - logging
        #  - monitoring
        dds.DomainParticipant.participant_factory_qos = qos_provider.participant_factory_qos_from_profile("Qos::Default")

        # Construct DDS entities
        self._participant = dds.DomainParticipant(
            self.domain_id,
            qos_provider.participant_qos_from_profile("Qos::Default")
        )

        self._command_writer = dds.DataWriter(
            dds.Topic(self._participant, "control/Command", control.Command),
            qos_provider.datawriter_qos_from_profile("Qos::Command")
        )
        self._set_priority(self.priority)

        self._state_reader = dds.DataReader(
            dds.Topic(self._participant, "control/State", control.State),
            qos_provider.datareader_qos_from_profile("Qos::State")
        )
        self._position_reader = dds.DataReader(
            dds.Topic(self._participant, "control/Position", control.Position),
            qos_provider.datareader_qos_from_profile("Qos::Stream")
        )
        self._alert_reader = dds.DataReader(
            dds.Topic(self._participant, "control/Alert", control.Alert),
            qos_provider.datareader_qos_from_profile("Qos::Event")
        )

    # --- Async tasks ---

    async def consume_alert(self):
        """Consume and log Alert samples asynchronously."""
        async for alert in self._alert_reader.take_data_async():
            print(f"\n  [alert] {alert.source_id}: level={alert.level} msg=\"{alert.message}\"")

    async def run(self):
        """Run the controller until cancelled."""
        loop = asyncio.get_event_loop()
        try:
            print(f"[controller] starting...")
            self._participant.enable()
            print(f"[controller] Ready.")
            self.do_help("")  # Show help on startup
            alert_task = asyncio.ensure_future(self.consume_alert())
            await loop.run_in_executor(None, self.cmdloop)
            alert_task.cancel()
        except asyncio.CancelledError:
            pass

    # --- Shell commands ---

    def emptyline(self):
        """Do nothing on empty input (override default repeat behavior)."""
        return False

    def do_list(self, _arg):
        """Show discovered targets (active targets marked with *)."""
        target_states = self._state_reader.read_data()
        if len(target_states) == 0:
            print("  No targets discovered yet.")
            return
        for target_state in target_states:
            if target_state.mode == control.ModeType.ACTIVE:
                marker = "*"
            elif target_state.mode == control.ModeType.IDLE:
                marker = " "
            elif target_state.mode == control.ModeType.ERROR:
                marker = "!"
            else:
                marker = "?"
            print(f"  [{marker}] {target_state.source_id}: mode={target_state.mode.name}")

    def do_positions(self, _arg):
        """Show current positions of all targets."""
        target_positions = self._position_reader.read_data()
        if len(target_positions) == 0:
            print("  No position data available yet.")
            return
        for pos in target_positions:
            print(f"  {pos.source_id}: ({pos.pose.x:.2f}, {pos.pose.y:.2f}, {pos.pose.z:.2f})")

    def do_send(self, arg):
        """Send command: send <target|all> <action> [x y z]"""
        try:
            parts = shlex.split(arg)
        except ValueError as e:
            print(f"  Error: {e}")
            return

        if len(parts) < 2:
            print("  Usage: send <target|all> <action> [x y z]")
            return

        target_str = parts[0]
        action_str = parts[1]

        action = self.ACTIONS.get(action_str.lower())
        if action is None:
            print(f"  Error: Unknown action '{action_str}'. Use: start, stop, pause")
            return

        # Parse optional pose
        pose = None
        if len(parts) >= 5:
            try:
                pose = control.Pose(x=float(parts[2]), y=float(parts[3]), z=float(parts[4]))
            except ValueError:
                print("  Error: x y z must be numbers")
                return

        if target_str.lower() == "all":
            self._send_command(target_id=control.ALL_TARGETS, action=action, pose=pose)
        else:
            known = [target_state.source_id for target_state in self._state_reader.read_data()]
            if target_str not in known:
                print(f"  Error: '{target_str}' not discovered. Use 'list' to see targets.")
                return
            self._send_command(target_id=target_str, action=action, pose=pose)

    def do_priority(self, arg):
        """Set controller priority: priority <int>"""
        try:
            new_priority = int(arg.strip())
            self._set_priority(new_priority)
            print(f"  Priority set to {new_priority}")
        except ValueError:
            print("  Error: Priority must be a non-negative integer")

    def do_quit(self, _arg):
        """Exit the controller."""
        return True

    do_EOF = do_quit

    # --- Helpers ---

    def _send_command(self, target_id: str, action: control.ActionType, pose: control.Pose):
        """Send a single command to a target."""
        self._command_writer.write(control.Command(target_id=target_id, action=action, target_pose=pose))
        print(f"  -> sent {action} to {target_id}")

    def _set_priority(self, priority: int):
        """Set the ownership strength (priority) for this controller's Command DataWriter."""
        self._command_writer.qos << dds.OwnershipStrength(priority)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run the Controller application.")
    parser.add_argument(
        "--priority", type=int, default=0, help="Priority of the Controller instance"
    )
    parser.add_argument("--domain", type=int, default=0, help="DDS domain ID")
    args = parser.parse_args()

    try:
        controller = Controller(priority=args.priority, domain_id=args.domain)
        asyncio.run(controller.run())
    except KeyboardInterrupt:
        pass
    except Exception as ex:
        print(f"Error: {ex}")
        sys.exit(1)