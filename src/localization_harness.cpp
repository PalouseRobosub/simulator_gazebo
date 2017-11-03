#include "ros/ros.h"
#include "robosub/Float32Stamped.h"
#include "tf/transform_listener.h"
#include "tf/transform_datatypes.h"

#include <string>

int main(int argc, char **argv)
{

    ros::init(argc, argv, "localization_harness");

    ros::NodeHandle nh;

    tf::TransformListener tflr;

    tf::StampedTransform resultantTransform;

    ros::Publisher loc_error_pub = nh.advertise<robosub::Float32Stamped>(
        "localization/error/linear", 1);

    // Wait for a transform to be available between the localization engine's
    // position and the simulator's position.

    ROS_DEBUG(
        "Waiting for available transformation from cobalt to cobalt_sim...");

    tflr.waitForTransform("cobalt", "cobalt_sim", ros::Time::now(),
        ros::Duration(0.01));

    while (ros::ok()) {

        tflr.lookupTransform("cobalt", "cobalt_sim", ros::Time(0),
            resultantTransform);

        double loc_error = resultantTransform.getOrigin().length();

        robosub::Float32Stamped loc_error_msg;

        loc_error_msg.data = loc_error;
        loc_error_msg.header.stamp = ros::Time::now();

        loc_error_pub.publish<robosub::Float32Stamped>(loc_error_msg);

    }

}