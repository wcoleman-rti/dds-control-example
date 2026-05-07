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

# --- C++ Controller application ---

add_executable(controller
    controller.cpp
)

target_link_libraries(controller
    PUBLIC
        ControlTypeSupport_cpp
)


# --- C++ Target application ---

add_executable(target
    target.cpp
)

target_link_libraries(target
    PUBLIC
        ControlTypeSupport_cpp
)