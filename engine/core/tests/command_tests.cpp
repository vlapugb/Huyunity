#include "huynity/core/scene.h"
#include "huynity/objectlib/primitives/point.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace core = huynity::core;
namespace math = huynity::math;
namespace objects = huynity::objectlib;

#define CHECK(...)                                                                                                     \
    do {                                                                                                               \
        if (!(__VA_ARGS__)) {                                                                                          \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " + #__VA_ARGS__);    \
        }                                                                                                              \
    } while (false)

core::ObjectId createObject(core::Scene& scene) {
    auto result = scene.createObject(objects::Point3::Params{.coordinates = {1, 2, 3}});
    CHECK(result.has_value());
    return *result;
}

void batchPreservesOrderAndRequestIds() {
    core::Scene scene;
    const auto id = createObject(scene);
    const std::vector<core::CommandRequest> requests{
        {41, core::SetVisibilityCommand{id, false}},
        {42, core::SetMobilityCommand{id, core::Mobility::Static}},
        {43, core::SetLocalTransformCommand{id, math::translation(math::Vector3{{2, 3, 4}})}},
        {44, core::SetLocalTransformCommand{id, math::translation(math::Vector3{{7, 8, 9}})}},
    };

    const auto results = scene.apply(requests);
    CHECK(results.size() == requests.size());
    for (std::size_t index = 0; index < results.size(); ++index) {
        CHECK(results[index].requestId == requests[index].requestId);
        CHECK(results[index].status.has_value());
        CHECK(!results[index].createdObject.has_value());
    }
    CHECK(!scene.find(id)->isVisible);
    CHECK(scene.find(id)->mobility == core::Mobility::Static);
    CHECK(std::get<math::Transform3>(scene.find(id)->worldTransform).translation(0) == 7);
    CHECK(std::get<math::Transform3>(scene.find(id)->worldTransform).translation(1) == 8);
    CHECK(std::get<math::Transform3>(scene.find(id)->worldTransform).translation(2) == 9);
}

void invalidCommandDoesNotMutateOrStopBatch() {
    core::Scene scene;
    const auto id = createObject(scene);
    const auto before = scene.find(id)->localTransform;
    const core::ObjectId missing{1000, 1};
    const std::vector<core::CommandRequest> requests{
        {7, core::SetLocalTransformCommand{missing, math::translation(math::Vector3{{9, 9, 9}})}},
        {8, core::SetVisibilityCommand{id, false}},
    };

    const auto results = scene.apply(requests);
    CHECK(results.size() == 2);
    CHECK(results[0].requestId == 7);
    CHECK(!results[0].status.has_value());
    CHECK(std::get<core::SceneError>(results[0].status.error()) == core::SceneError::InvalidObjectId);
    CHECK(results[1].requestId == 8);
    CHECK(results[1].status.has_value());
    CHECK(scene.find(id)->localTransform == before);
    CHECK(!scene.find(id)->isVisible);
}

void creationCommandsReturnIdsAndErrors() {
    core::Scene scene;
    const std::vector<core::CommandRequest> requests{
        {100, core::CreateObjectCommand{objects::Point3::Params{.coordinates = {1, 2, 3}}}},
        {101, core::CreateObjectCommand{objects::Circle2::Params{.radius = 0}}},
    };
    const auto results = scene.apply(requests);
    CHECK(results.size() == 2);
    CHECK(results[0].status.has_value());
    CHECK(results[0].createdObject.has_value());
    CHECK(scene.find(*results[0].createdObject) != nullptr);
    CHECK(!results[1].status.has_value());
    CHECK(std::holds_alternative<objects::CreateError>(results[1].status.error()));
    CHECK(!results[1].createdObject.has_value());
    CHECK(scene.size() == 1);

    const std::vector<core::CommandRequest> copy{{102, core::CreateInstanceCommand{*results[0].createdObject}}};
    const auto copied = scene.apply(copy);
    CHECK(copied[0].status.has_value());
    CHECK(copied[0].createdObject.has_value());
    CHECK(scene.find(*copied[0].createdObject)->geometry == scene.find(*results[0].createdObject)->geometry);
}

void sceneViewIsIndependentAndReadOnlyTowardsCore() {
    core::Scene scene;
    const auto id = createObject(scene);
    auto view = scene.view();
    static_assert(!std::is_reference_v<decltype(view.instances)>);
    static_assert(std::is_const_v<std::remove_pointer_t<decltype(view.instances[0].geometry.get())>>);
    CHECK(view.instances.size() == 1);
    CHECK(view.instances[0].id == id);

    view.instances[0].localTransform = math::translation(math::Vector3{{8, 0, 0}});
    view.instances[0].isVisible = false;
    view.instances.clear();
    CHECK(scene.size() == 1);
    CHECK(scene.find(id)->localTransform == core::Transform{math::Transform3{}});
    CHECK(scene.find(id)->isVisible);
}

int main() {
    try {
        batchPreservesOrderAndRequestIds();
        invalidCommandDoesNotMutateOrStopBatch();
        creationCommandsReturnIdsAndErrors();
        sceneViewIsIndependentAndReadOnlyTowardsCore();
        std::cout << "commands: OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
