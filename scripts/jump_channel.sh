#!/bin/bash
# this script will jump the sub
rosservice call /gazebo/set_model_state "model_state:
  model_name: 'robosub'
  pose:
    position:
      x: 26
      y: -16.9
      z: -2.7
    orientation:
      x: 0.0
      y: 0.0
      z: 0.54
      w: 0.84"
rostopic pub --once /control robosub/control "
forward_state: 3
strafe_state: 3
dive_state: 1
roll_state: 1
pitch_state: 1
yaw_state: 1
forward: 0.0
strafe_left: 0.0
dive: -1.8
roll_right: 0.0
pitch_down: 0.0
yaw_left: 64"
