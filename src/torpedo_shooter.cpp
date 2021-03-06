#include "torpedo_shooter.h"

namespace gazebo
{
bool TorpedoShooter::shoot(std_srvs::Trigger::Request  &req,
                         std_srvs::Trigger::Response &res)
{
    // Need to make a torpedo model
    physics::ModelPtr torpedo = world->GetModel("torpedo");

    //Check if the torpedo model is nullptr
    if(torpedo == nullptr)
    {
        ROS_ERROR("Torpedo pointer is Null");
        return false;
    }

    math::Pose sub_position = sub->GetWorldPose();

    ROS_INFO("Current Sub Position x: %f, y: %f, z: %f", sub_position.pos.x,
              sub_position.pos.y, sub_position.pos.z);

    ROS_INFO("Current Sub rotation roll: %f, pitch: %f, yaw: %f",
        sub_position.rot.GetAsEuler().x, sub_position.rot.GetAsEuler().y,
        sub_position.rot.GetAsEuler().z);

    //This sets the torpedo spawn position relative to the sub
    sub_position.pos.z -= 0.4;
    //sub_position.pos.y += 1;
    // This will be where the sub velocity is added to the marker
    // torpedo->SetLinearAccel(math::Vector3(0.0,0.0,.001));


    // This is for calculating the direction of the initial velocity of
    // the torpedo
    // Using just trig
    math::Vector3 magnitude =
                  math::Vector3(cos(sub_position.rot.GetAsEuler().z),
                      sin(sub_position.rot.GetAsEuler().z), 0.0);

    torpedo->SetLinearVel(magnitude);
    torpedo->SetWorldPose(sub_position);

    ROS_INFO("Torpedo Fired!");
    res.success = true;
    res.message = "Torpedoes Away!";
    return true;
}

void TorpedoShooter::Load(physics::ModelPtr _parent, sdf::ElementPtr)
{
    sub = _parent;
    world = sub->GetWorld();

    int argc = 0;
    char **argv = NULL;
    ros::init(argc, argv, "torpedo_shooter",
        ros::init_options::NoSigintHandler);

    if(!ros::isInitialized())
    {
        std::cerr << "ROS Not initialized" << std::endl;
        return;
    }

    ROS_INFO_STREAM("ROS Initialized!");
    nh = new ros::NodeHandle();

    world->InsertModelFile("model://torpedo");
    ros::AdvertiseServiceOptions shoot_torpedo_aso =
        ros::AdvertiseServiceOptions::create<std_srvs::Trigger>("shoot_torpedo",
        boost::bind(&TorpedoShooter::shoot, this, _1, _2),
        ros::VoidPtr(), &service_callback_queue);

    service_server = nh->advertiseService(shoot_torpedo_aso);

    ROS_INFO_STREAM(service_server.getService() << " started");

    updateConnection = event::Events::ConnectWorldUpdateBegin(
    boost::bind(&TorpedoShooter::Update, this));

    ROS_INFO("Torpedo Shooter Plugin loaded");
}

void TorpedoShooter::Update()
{
    service_callback_queue.callAvailable();
}


// Register this plugin with the simulator
GZ_REGISTER_MODEL_PLUGIN(TorpedoShooter)
}
