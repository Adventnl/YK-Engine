#include "WorldImpl.hpp"
#include <numbers>

namespace yk::physics {
Result<JointHandle> World::createDistanceJoint(const DistanceJointDef &definition) {
    if (!valid(definition.first) || !valid(definition.second))
        return Error{"Invalid, destroyed, or foreign physics handle"};
    if (definition.first == definition.second || !bounded(definition.localAnchorFirst) ||
        !bounded(definition.localAnchorSecond) ||
        (!bounded(definition.length) || definition.length < 0.005F) ||
        !nonnegative(definition.springHertz) || !nonnegative(definition.dampingRatio))
        return Error{"Invalid distance joint definition"};
    auto &first = impl_->bodies.at(definition.first.serial_);
    auto &second = impl_->bodies.at(definition.second.serial_);
    if (b2Body_GetType(first.nativeId) != b2_dynamicBody &&
        b2Body_GetType(second.nativeId) != b2_dynamicBody)
        return Error{"A joint requires at least one dynamic body"};
    auto joint = b2DefaultDistanceJointDef();
    joint.bodyIdA = first.nativeId;
    joint.bodyIdB = second.nativeId;
    joint.localAnchorA = physics::native(definition.localAnchorFirst);
    joint.localAnchorB = physics::native(definition.localAnchorSecond);
    joint.length = definition.length;
    joint.enableSpring = definition.springHertz > 0;
    joint.hertz = definition.springHertz;
    joint.dampingRatio = definition.dampingRatio;
    joint.collideConnected = definition.collideConnected;
    const auto id = b2CreateDistanceJoint(impl_->world, &joint);
    const auto handle = impl_->handle<JointTag>();
    impl_->joints.emplace(handle.serial_, Impl::Joint{id, definition.first, definition.second});
    first.joints.push_back(handle);
    second.joints.push_back(handle);
    return handle;
}
Result<JointHandle> World::createRevoluteJoint(const RevoluteJointDef &definition) {
    if (!valid(definition.first) || !valid(definition.second))
        return Error{"Invalid, destroyed, or foreign physics handle"};
    constexpr float angleLimit = 0.99F * std::numbers::pi_v<float>;
    if (definition.first == definition.second || !bounded(definition.localAnchorFirst) ||
        !bounded(definition.localAnchorSecond) || !bounded(definition.referenceAngle) ||
        !bounded(definition.lowerAngle) || !bounded(definition.upperAngle) ||
        definition.lowerAngle < -angleLimit || definition.upperAngle > angleLimit ||
        definition.lowerAngle > definition.upperAngle || !bounded(definition.motorSpeed) ||
        !nonnegative(definition.maxMotorTorque))
        return Error{"Invalid revolute joint definition or angle limits"};
    auto &first = impl_->bodies.at(definition.first.serial_);
    auto &second = impl_->bodies.at(definition.second.serial_);
    if (b2Body_GetType(first.nativeId) != b2_dynamicBody &&
        b2Body_GetType(second.nativeId) != b2_dynamicBody)
        return Error{"A joint requires at least one dynamic body"};
    auto joint = b2DefaultRevoluteJointDef();
    joint.bodyIdA = first.nativeId;
    joint.bodyIdB = second.nativeId;
    joint.localAnchorA = physics::native(definition.localAnchorFirst);
    joint.localAnchorB = physics::native(definition.localAnchorSecond);
    joint.referenceAngle = definition.referenceAngle;
    joint.enableLimit = definition.enableLimit;
    joint.lowerAngle = definition.lowerAngle;
    joint.upperAngle = definition.upperAngle;
    joint.enableMotor = definition.enableMotor;
    joint.motorSpeed = definition.motorSpeed;
    joint.maxMotorTorque = definition.maxMotorTorque;
    joint.collideConnected = definition.collideConnected;
    const auto id = b2CreateRevoluteJoint(impl_->world, &joint);
    const auto handle = impl_->handle<JointTag>();
    impl_->joints.emplace(handle.serial_, Impl::Joint{id, definition.first, definition.second});
    first.joints.push_back(handle);
    second.joints.push_back(handle);
    return handle;
}
} // namespace yk::physics
