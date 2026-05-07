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
import sys
from dataclasses import dataclass, field
from pathlib import Path

import rti.asyncio
import rti.connextdds as dds

sys.path.append(str(Path(__file__).resolve().parent.parent / "interface" / "control"))
from Control import control



@dataclass
class Target:
    uid: str
    mode: control.ModeType = control.ModeType.IDLE
    position: control.Position = field(default_factory=control.Position)
    target_pose: control.Pose = field(default_factory=control.Pose)
    arrived: bool = True
    domain_id: int = 0
    stop_requested: bool = False

    # DDS entities — set up in __post_init__
    _participant: dds.DomainParticipant = field(init=False, repr=False)
    _command_reader: dds.DataReader = field(init=False, repr=False)
    _state_writer: dds.DataWriter = field(init=False, repr=False)
    _position_writer: dds.DataWriter = field(init=False, repr=False)
    _alert_writer: dds.DataWriter = field(init=False, repr=False)

    def __post_init__(self):
        """Initialize DDS entities"""
        self.position.source_id = self.uid

        qos_provider = dds.QosProvider.default

        # Participant Factory Qos defines:
        #  - entity enabling (explicitly or automatically on creation)
        #  - logging
        #  - monitoring
        dds.DomainParticipant.participant_factory_qos = qos_provider.participant_factory_qos_from_profile("Qos::Default")

        self._participant = dds.DomainParticipant(
            self.domain_id,
            qos_provider.participant_qos_from_profile("Qos::Default")
        )

        self._command_reader = dds.DataReader(
            dds.ContentFilteredTopic(
                dds.Topic(self._participant, "control/Command", control.Command),
                f"Command@{self.uid}",
                dds.Filter(f"target_id = %0 or target_id = '{control.ALL_TARGETS}'", [f"'{self.uid}'"]),
            ),
            qos_provider.datareader_qos_from_profile("Qos::Command")
        )
        self._state_writer = dds.DataWriter(
            dds.Topic(self._participant, "control/State", control.State),
            qos_provider.datawriter_qos_from_profile("Qos::State")
        )
        self._position_writer = dds.DataWriter(
            dds.Topic(self._participant, "control/Position", control.Position),
            qos_provider.datawriter_qos_from_profile("Qos::Stream")
        )
        self._alert_writer = dds.DataWriter(
            dds.Topic(self._participant, "control/Alert", control.Alert),
            qos_provider.datawriter_qos_from_profile("Qos::Event")
        )

    async def consume_commands(self):
        """Consume Command samples and publish responses."""
        async for cmd in self._command_reader.take_data_async():
            if cmd.target_id not in (self.uid, control.ALL_TARGETS):
                print(f"Received command for target_id={cmd.target_id}, expected {self.uid}", file=sys.stderr)
                continue

            print(f"[target {self.uid}] received command action={cmd.action.name} target_pose=({cmd.target_pose.x if cmd.target_pose else 'N/A'}, {cmd.target_pose.y if cmd.target_pose else 'N/A'}, {cmd.target_pose.z if cmd.target_pose else 'N/A'})")

            if cmd.action == control.ActionType.STOP:
                self._alert_writer.write(control.Alert(
                    source_id=self.uid,
                    level=control.AlertLevel.WARNING,
                    message="Stop command received",
                ))

                await asyncio.sleep(0.1)  # Simulate time to process stop command
                self.mode = control.ModeType.IDLE
                self._state_writer.write(control.State(source_id=self.uid, mode=self.mode))
                self._state_writer.lookup_instance(control.State(source_id=self.uid, mode=self.mode))
                self._state_writer.dispose_instance(
                    self._state_writer.lookup_instance(control.State(source_id=self.uid, mode=self.mode)))  # Indicate target is no longer active

                self.stop_requested = True
                return  # Exit after processing STOP command

            elif cmd.action == control.ActionType.START:
                if self.mode != control.ModeType.ACTIVE:
                    self.mode = control.ModeType.ACTIVE
                    self._state_writer.write(control.State(source_id=self.uid, mode=self.mode))

            elif cmd.action == control.ActionType.PAUSE:
                if self.mode == control.ModeType.ACTIVE:
                    self.mode = control.ModeType.IDLE
                    self._state_writer.write(control.State(source_id=self.uid, mode=self.mode))

            else:
                self.mode = control.ModeType.ERROR
                self._state_writer.write(control.State(source_id=self.uid, mode=self.mode))
                self.stop_requested = True
                raise ValueError(f"Received command with unknown action={cmd.action}")
            

            if cmd.target_pose:
                self.target_pose = cmd.target_pose
                self.arrived = False


    async def run(self, rate: float = 0.1):
        """Run the target until cancelled."""
        try:
            print(f"[target {self.uid}] starting with initial position ({self.position.pose.x}, {self.position.pose.y}, {self.position.pose.z})")
            self._participant.enable()
            self._state_writer.write(control.State(source_id=self.uid, mode=self.mode))  # Publish initial state
            
            command_task = asyncio.ensure_future(self.consume_commands())

            i = 0
            while not self.stop_requested and self.mode in (control.ModeType.ACTIVE, control.ModeType.IDLE):
                # Periodically print position
                if i % 5 == 0:
                    print(f"[target {self.uid}] position=({self.position.pose.x:.2f}, {self.position.pose.y:.2f}, {self.position.pose.z:.2f})", end="", flush=False)
                    if self.arrived:
                        print(f" (arrived at dest)")
                    else:
                        print(f" dest=({self.target_pose.x:.2f}, {self.target_pose.y:.2f}, {self.target_pose.z:.2f})")
                i += 1

                # Publish current state and position
                self._position_writer.write(self.position)

                # Simulate target movement by updating position based on target_pose
                if self.mode == control.ModeType.ACTIVE and not self.arrived:
                    self.position.pose.x += (self.target_pose.x - self.position.pose.x) * rate
                    self.position.pose.y += (self.target_pose.y - self.position.pose.y) * rate
                    self.position.pose.z += (self.target_pose.z - self.position.pose.z) * rate

                    if (abs(self.position.pose.x - self.target_pose.x) < 0.01 and
                        abs(self.position.pose.y - self.target_pose.y) < 0.01 and
                        abs(self.position.pose.z - self.target_pose.z) < 0.01):

                        # Target has arrived at the target position
                        self.arrived = True
                        self.position.pose.x = self.target_pose.x
                        self.position.pose.y = self.target_pose.y
                        self.position.pose.z = self.target_pose.z
                        self._position_writer.write(self.position)  # Publish final position update

                        # Update state to IDLE upon arrival at target position
                        self.mode = control.ModeType.IDLE
                        self._state_writer.write(control.State(source_id=self.uid, mode=self.mode))
                        
                        # Publish an alert indicating arrival at target position
                        self._alert_writer.write(control.Alert(
                            source_id=self.uid,
                            level=control.AlertLevel.INFO,
                            message="Arrived at target position",
                        ))

                # Simulate time delay between updates
                await asyncio.sleep(1.0)

            command_task.cancel()

        except asyncio.CancelledError:
            pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Run the Target application.")
    parser.add_argument(
        "--uid", type=str, default="target-1", help="Unique identifier for the Target"
    )
    parser.add_argument("--domain", type=int, default=0, help="DDS domain ID")
    args = parser.parse_args()

    try:
        target = Target(uid=args.uid, domain_id=args.domain)
        asyncio.run(target.run())
    except KeyboardInterrupt:
        pass
    except Exception as ex:
        print(f"Error: {ex}")
        sys.exit(1)
