#include "ros/ros.h"
#include "robosub/depth_stamped.h"
#include "robosub/Euler.h"
#include "geometry_msgs/Vector3.h"
#include "gazebo_msgs/ModelState.h"
#include "gazebo_msgs/ModelStates.h"
#include "robosub/ObstaclePosArray.h"
#include "robosub/QuaternionStampedAccuracy.h"
#include "robosub/HydrophoneDeltas.h"
#include "tf/transform_datatypes.h"

#include <cmath>
#include <vector>
#include <eigen3/Eigen/Dense>
#include <eigen3/Eigen/Geometry>

using std::vector;
using namespace Eigen;

static constexpr double _180_OVER_PI = 180.0 / 3.14159;

ros::Publisher position_pub;
ros::Publisher orientation_pub;
ros::Publisher euler_pub;
ros::Publisher depth_pub;
ros::Publisher obstacle_pos_pub;
ros::Publisher hydrophone_deltas_pub;

// List of names of objects to publish the position and name of. This will be
// loaded from parameters.
std::vector<std::string> object_names;

// ModelStates msg consists of a name, a pose (position and orientation),
// and a twist (linear and angular velocity) for each object in
// the simulator
// Currently it publishes the position, orientation, and depth
// of the sub
// TODO: Eventually this should be generalized to publish a list
// (defined in params) of objects as well as calulate depth
void modelStatesCallback(const gazebo_msgs::ModelStates& msg)
{
    geometry_msgs::Vector3 position_msg;
    robosub::QuaternionStampedAccuracy orientation_msg;
    robosub::depth_stamped depth_msg;
    robosub::Euler euler_msg;
    vector<Vector3d> hydrophone_positions;

    // Find top of water and subs indices in modelstates lists
    int sub_index = -1, pinger_index = -1, ceiling_index = -1;
    for(int i=0; i<msg.name.size(); i++)
    {
        if (msg.name[i] == "robosub") sub_index = i;
        if (msg.name[i] == "ceiling_plane") ceiling_index = i;
        if (msg.name[i] == "pinger_a") pinger_index = i;
    }

    if(sub_index >= 0)
    {
        // Copy sub pos to position msg
        position_msg.x = msg.pose[sub_index].position.x;
        position_msg.y = msg.pose[sub_index].position.y;
        position_msg.z = msg.pose[sub_index].position.z;

        // Copy sub orientation to orientation msg
        orientation_msg.quaternion.x = msg.pose[sub_index].orientation.x;
        orientation_msg.quaternion.y = msg.pose[sub_index].orientation.y;
        orientation_msg.quaternion.z = msg.pose[sub_index].orientation.z;
        orientation_msg.quaternion.w = msg.pose[sub_index].orientation.w;
        orientation_msg.header.stamp = ros::Time::now();
        orientation_msg.accuracy = 1;

        // Create an Euler message for human readability
        tf::Matrix3x3 m(tf::Quaternion(orientation_msg.quaternion.x,
                    orientation_msg.quaternion.y, orientation_msg.quaternion.z,
                    orientation_msg.quaternion.w));
        m.getRPY(euler_msg.roll, euler_msg.pitch, euler_msg.yaw);
        euler_msg.roll *= _180_OVER_PI;
        euler_msg.pitch *= _180_OVER_PI;
        euler_msg.yaw *= _180_OVER_PI;
        euler_pub.publish(euler_msg);

        // Publish sub position and orientation
        position_pub.publish(position_msg);
        orientation_pub.publish(orientation_msg);

        // If the model for the top of the water is found calculate depth from
        // the z positions of the water top and the sub
        if(ceiling_index >= 0)
        {
            depth_msg.depth = -(msg.pose[ceiling_index].position.z -
                             msg.pose[sub_index].position.z);
            depth_msg.header.stamp = ros::Time::now();
            depth_pub.publish(depth_msg);
        }

        /*
         * If the pinger index isn't found, break out and dont perform
         * hydrophone calculations.
         */
        if (pinger_index > -1)
        {
            /*
             * Once the submarine position and orientation are known, calculate
             * all of the hydrophone positions. Assume the first hydrophone is
             * 0.5 meters directly in front of the submarine, and that the 3
             * other reference hydrophones are 0.3 meters along the primary
             * axes. Begin by setting the hydrophone positions as relative to
             * the submarine without any rotations applied. They are stored in
             * order:
             *      reference, x-axis, y-axis, z-axis
             */
            hydrophone_positions.push_back(Vector3d(0.5, 0, 0));
            hydrophone_positions.push_back(Vector3d(0.8, 0, 0));
            hydrophone_positions.push_back(Vector3d(0.5, 0.3, 0));
            hydrophone_positions.push_back(Vector3d(0.5, 0, 0.3));

            /*
             * Next, create a yaw, pitch, and roll rotation matrix to calculate
             * the rotated positions relative to the sub. Eigen can represent a
             * rotation matrix as a quaternion. Then, once they are rotated,
             * translate them to be with respect to the global frame by adding
             * in the x, y, and z positions of the submarine.
             */
            Eigen::Quaterniond q(orientation_msg.quaternion.w,
                    orientation_msg.quaternion.x, orientation_msg.quaternion.y,
                    orientation_msg.quaternion.z);

            for (int i = 0; i < 4; ++i)
            {
                hydrophone_positions[i] = q.toRotationMatrix() *
                        hydrophone_positions[i];
                hydrophone_positions[i] += Vector3d(position_msg.x,
                        position_msg.y, position_msg.z);
            }

            /*
             * Now that hydrophone positions have been calculated, find the
             * distance of each hydrophone from the pinger and translate the
             * distance into signal time-of-flight as the ping travels through
             * the water.
             */
            vector<double> hydrophone_time_delays;
            Vector3d pinger_position(msg.pose[pinger_index].position.x,
                    msg.pose[pinger_index].position.y,
                    msg.pose[pinger_index].position.z);

            for (int i = 0; i < hydrophone_positions.size(); ++i)
            {
                Vector3d delta = hydrophone_positions[i] - pinger_position;
                double distance = sqrt(delta[0]*delta[0] + delta[1] * delta[1]
                        + delta[2] * delta[2]);

                /*
                 * Calculate a ping signal time delay from the distance by
                 * dividing by the speed of sound in water (1484 m/s).
                 */
                double time_delay = distance / 1484.0;
                hydrophone_time_delays.push_back(time_delay);
            }

            /*
             * Subtract the reference time delay from each hyodrophone time
             * delay to calculate the time differences in receiving the
             * hydrophone signal on all of the ordinal hydrophones.
             */
            for (unsigned int i = 1; i < hydrophone_time_delays.size(); ++i)
            {
                hydrophone_time_delays[i] -= hydrophone_time_delays[0];
            }

            robosub::HydrophoneDeltas deltas;
            deltas.header.stamp = ros::Time::now();
            deltas.xDelta = ros::Duration(hydrophone_time_delays[1]);
            deltas.yDelta = ros::Duration(hydrophone_time_delays[2]);
            deltas.zDelta = ros::Duration(hydrophone_time_delays[3]);

            hydrophone_deltas_pub.publish(deltas);
        }
    }

    // Iterate through object_names and for each iteration search through the
    // msg.name array and attempt to find object_names[i]. If it is not found
    // std::find returns an iterator equal to msg.name.end(). If it is found
    // std::find returns an iterator pointing to the object with
    // object_name[i]. std::distance is used to find the index of that object
    // wthin the msg.name (and therefore msg.pose array since they contain the
    // same objects in the same order).
    robosub::ObstaclePosArray object_array;
    for(int i=0; i<object_names.size(); i++)
    {
        auto it = std::find(msg.name.begin(), msg.name.end(), object_names[i]);

        // object_name[i] found in msg.name.
        if(it != msg.name.end())
        {
            // Calculate index of object in msg.name from the iterator element.
            int idx = std::distance(msg.name.begin(), it);

            // Extract necessary data from modelstates.
            robosub::ObstaclePos pos;
            pos.x = msg.pose[idx].position.x;
            pos.y = msg.pose[idx].position.y;
            pos.z = msg.pose[idx].position.z;
            pos.name = msg.name[idx];

            object_array.data.push_back(pos);
        }
    }

    obstacle_pos_pub.publish(object_array);
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "simulator_bridge");

    ros::NodeHandle nh;

    position_pub = nh.advertise<geometry_msgs::Vector3>("position", 1);
    orientation_pub =
            nh.advertise<robosub::QuaternionStampedAccuracy>("orientation", 1);
    euler_pub = nh.advertise<robosub::Euler>( "orientation/pretty", 1);
    depth_pub = nh.advertise<robosub::depth_stamped>("depth", 1);
    obstacle_pos_pub =
            nh.advertise<robosub::ObstaclePosArray>("obstacles/positions", 1);
    hydrophone_deltas_pub = nh.advertise<robosub::HydrophoneDeltas>(
            "hydrophone/30khz/delta", 1);

    ros::Subscriber orient_sub = nh.subscribe("gazebo/model_states", 1,
            modelStatesCallback);

    int rate;
    if(!nh.getParam("control/rate", rate))
    {
        rate = 30;
    }
    ros::Rate r(rate);

    // Put object names in vector.
    if(!nh.getParam("/obstacles", object_names))
    {
        ROS_WARN_STREAM("failed to load obstacle names");
    }

    // I use spinOnce and sleeps here because the simulator publishes
    // at a very high rate and we don't need every message.
    // You can throttle subscriber input in other ways but this method doesn't
    // require extra packages or anything like that.
    while(ros::ok())
    {
        ros::spinOnce();
        r.sleep();
    }

    return 0;
}