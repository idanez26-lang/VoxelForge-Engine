#include "EditorCamera.h"
#include "EditorMatrix.h"
#include "VoxelSelection/ViewportRayBuilder.h"
#include "VoxelSelection/VoxelRaycast.h"
#include "VoxelSelection/VoxelSelectionState.h"

#include "VoxelForge/Asset/Vox/VoxFormat.h"
#include "VoxelForge/Asset/Voxel/VoxDocumentLoader.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using namespace VoxelForge;

void Require(const bool condition, const std::string_view message)
{
    if (!condition) throw std::runtime_error(std::string(message));
}

bool Near(const float left, const float right, const float epsilon = 0.002F)
{
    return std::abs(left - right) <= epsilon;
}

bool Same(const Editor::Vec3 left, const Editor::Vec3 right)
{
    return Near(left.X, right.X) && Near(left.Y, right.Y) &&
        Near(left.Z, right.Z);
}

Asset::Voxel::VoxelDocument Document(
    std::vector<Asset::Vox::VoxModelMetadata> models)
{
    Asset::Vox::VoxModel source;
    source.Version = 150U;
    source.Palette = Asset::Vox::DefaultVoxPalette();
    source.Models = std::move(models);
    source.DeclaredModelCount = static_cast<std::uint32_t>(source.Models.size());
    source.HasPackChunk = source.Models.size() > 1U;
    auto loaded = Asset::Voxel::VoxDocumentLoader{}.Build(
        source, "ray-picking-memory.vox");
    Require(loaded.Succeeded(), "Unable to create ray-picking fixture.");
    return std::move(*loaded.Document);
}

Asset::Vox::VoxModelMetadata Model(
    const Asset::Vox::VoxDimensions dimensions,
    std::vector<Asset::Vox::VoxVoxel> voxels)
{
    return {dimensions, std::move(voxels)};
}

Editor::VoxelRay WorldRayForLocal(
    const Editor::VoxelModelTransform& transform,
    const Editor::Vec3 localOrigin,
    const Editor::Vec3 localDirection)
{
    return {
        Editor::TransformPoint(transform.ModelMatrix, localOrigin),
        Editor::Normalize(
            Editor::TransformVector(transform.ModelMatrix, localDirection))};
}

void TestViewportRayBuilder()
{
    Editor::EditorCamera camera;
    camera.Frame(3.0F, 3.0F, 3.0F);
    camera.SetView(Editor::EditorCameraView::Front);
    camera.SetAspectRatio(2.0F);
    const Editor::ViewportRectangle viewport{100.0F, 50.0F, 800.0F, 400.0F};
    const Editor::Matrix4 viewProjection = camera.GetViewProjection();
    const auto center = Editor::BuildViewportRay(
        {500.0F, 250.0F}, viewport, viewProjection, camera.GetPosition());
    Require(center.Succeeded() &&
        Same(center.Ray->Origin, camera.GetPosition()) &&
        Same(center.Ray->Direction, camera.GetForward()) &&
        Near(Editor::Length(center.Ray->Direction), 1.0F) &&
        Editor::IsFinite(center.Ray->Direction),
        "Center viewport ray is not normalized or camera-aligned.");

    const auto topLeft = Editor::BuildViewportRay(
        {100.0F, 50.0F}, viewport, viewProjection, camera.GetPosition());
    const auto topRight = Editor::BuildViewportRay(
        {900.0F, 50.0F}, viewport, viewProjection, camera.GetPosition());
    const auto bottomLeft = Editor::BuildViewportRay(
        {100.0F, 450.0F}, viewport, viewProjection, camera.GetPosition());
    const auto bottomRight = Editor::BuildViewportRay(
        {900.0F, 450.0F}, viewport, viewProjection, camera.GetPosition());
    Require(topLeft.Succeeded() && topRight.Succeeded() &&
        bottomLeft.Succeeded() && bottomRight.Succeeded() &&
        Editor::Dot(topLeft.Ray->Direction, camera.GetRight()) < 0.0F &&
        Editor::Dot(topLeft.Ray->Direction, camera.GetUp()) > 0.0F &&
        Editor::Dot(topRight.Ray->Direction, camera.GetRight()) > 0.0F &&
        Editor::Dot(topRight.Ray->Direction, camera.GetUp()) > 0.0F &&
        Editor::Dot(bottomLeft.Ray->Direction, camera.GetRight()) < 0.0F &&
        Editor::Dot(bottomLeft.Ray->Direction, camera.GetUp()) < 0.0F &&
        Editor::Dot(bottomRight.Ray->Direction, camera.GetRight()) > 0.0F &&
        Editor::Dot(bottomRight.Ray->Direction, camera.GetUp()) < 0.0F,
        "Viewport corner rays use incorrect screen/NDC orientation.");
    const auto repeated = Editor::BuildViewportRay(
        {100.0F, 50.0F}, viewport, viewProjection, camera.GetPosition());
    Require(repeated.Succeeded() && repeated.Ray == topLeft.Ray,
        "Viewport ray construction is not deterministic.");

    camera.SetAspectRatio(0.5F);
    const auto resized = Editor::BuildViewportRay(
        {300.0F, 250.0F}, {100.0F, 50.0F, 400.0F, 800.0F},
        camera.GetViewProjection(), camera.GetPosition());
    Require(resized.Succeeded() &&
        Editor::Dot(resized.Ray->Direction, camera.GetRight()) > 0.0F,
        "Resized viewport did not produce a valid ray.");

    Require(Editor::BuildViewportRay(
        {99.0F, 50.0F}, viewport, viewProjection,
        camera.GetPosition()).Error ==
            Editor::ViewportRayBuildError::OutsideViewport,
        "Mouse outside viewport must be rejected.");
    Require(Editor::BuildViewportRay(
        {100.0F, 50.0F}, {100.0F, 50.0F, 0.0F, 400.0F},
        viewProjection, camera.GetPosition()).Error ==
            Editor::ViewportRayBuildError::InvalidInput,
        "Zero-sized viewport must be rejected.");
    const Editor::Matrix4 singular{};
    Require(Editor::BuildViewportRay(
        {500.0F, 250.0F}, viewport, singular,
        camera.GetPosition()).Error ==
            Editor::ViewportRayBuildError::NonInvertibleViewProjection,
        "Non-invertible camera matrix must be rejected.");
    const float infinity = std::numeric_limits<float>::infinity();
    Require(Editor::BuildViewportRay(
        {infinity, 250.0F}, viewport, viewProjection,
        camera.GetPosition()).Error == Editor::ViewportRayBuildError::InvalidInput,
        "Non-finite viewport input must be rejected.");

    const Editor::Vec3 initialPosition = camera.GetPosition();
    camera.Pan(80.0F, -40.0F, 400.0F);
    camera.SetView(Editor::EditorCameraView::Right);
    camera.SetAspectRatio(2.0F);
    const auto moved = Editor::BuildViewportRay(
        {500.0F, 250.0F}, viewport, camera.GetViewProjection(),
        camera.GetPosition());
    Require(moved.Succeeded() &&
        !Same(moved.Ray->Origin, initialPosition) &&
        Same(moved.Ray->Direction, camera.GetForward()),
        "Moved/rotated current perspective camera ray is incorrect.");
}

void RequireAxisHit(
    const Asset::Voxel::VoxelDocument& document,
    const Editor::VoxelRay ray,
    const Editor::VoxelHitFace face,
    const Asset::Voxel::VoxelPosition adjacent)
{
    const auto hit = Editor::RaycastVoxelDocument(document, ray);
    Require(hit && hit->Coordinates == Editor::VoxelCoordinates{1U, 1U, 1U} &&
        hit->Face == face && hit->AdjacentPosition == adjacent &&
        Same(hit->Normal, Editor::VoxelHitFaceNormal(face)) &&
        std::isfinite(hit->Distance) && hit->Distance >= 0.0F,
        "Axis ray returned incorrect hit metadata.");
}

void TestBoundsDdaAndResult()
{
    Asset::Voxel::VoxelDocument document = Document({
        Model({3U, 3U, 3U}, {{1U, 1U, 1U, 12U}})});
    const std::uint64_t revision = document.GetRevision();
    const bool dirty = document.IsDirty();
    RequireAxisHit(document, {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}},
        Editor::VoxelHitFace::NegativeX, {0, 1, 1});
    RequireAxisHit(document, {{5.0F, 1.5F, 1.5F}, {-1.0F, 0.0F, 0.0F}},
        Editor::VoxelHitFace::PositiveX, {2, 1, 1});
    RequireAxisHit(document, {{1.5F, -2.0F, 1.5F}, {0.0F, 1.0F, 0.0F}},
        Editor::VoxelHitFace::NegativeY, {1, 0, 1});
    RequireAxisHit(document, {{1.5F, 5.0F, 1.5F}, {0.0F, -1.0F, 0.0F}},
        Editor::VoxelHitFace::PositiveY, {1, 2, 1});
    RequireAxisHit(document, {{1.5F, 1.5F, -2.0F}, {0.0F, 0.0F, 1.0F}},
        Editor::VoxelHitFace::NegativeZ, {1, 1, 0});
    RequireAxisHit(document, {{1.5F, 1.5F, 5.0F}, {0.0F, 0.0F, -1.0F}},
        Editor::VoxelHitFace::PositiveZ, {1, 1, 2});

    const auto inside = Editor::RaycastVoxelDocument(
        document, {{1.25F, 1.25F, 1.25F}, {1.0F, 0.0F, 0.0F}});
    Require(inside && Near(inside->Distance, 0.0F) &&
        inside->Face == Editor::VoxelHitFace::None &&
        inside->AdjacentPosition == Asset::Voxel::VoxelPosition{1, 1, 1} &&
        !inside->AdjacentWithinBounds && Same(inside->Normal, {}) &&
        Same(inside->LocalPosition, {1.25F, 1.25F, 1.25F}) &&
        Same(inside->WorldPosition, inside->LocalPosition),
        "Ray starting in occupied voxel has unsafe or incorrect result.");
    Require(!Editor::RaycastVoxelDocument(
        document, {{-2.0F, 0.5F, 0.5F}, {1.0F, 0.0F, 0.0F}}),
        "Ray through empty cells must miss.");
    Require(!Editor::RaycastVoxelDocument(
        document, {{-2.0F, 8.0F, 1.5F}, {1.0F, 0.0F, 0.0F}}),
        "Parallel ray outside bounds must miss.");
    Require(!Editor::RaycastVoxelDocument(
        document, {{-2.0F, 1.5F, 1.5F}, {0.0F, 0.0F, 0.0F}}),
        "Zero direction must miss.");

    Editor::VoxelRaycastOptions shortDistance;
    shortDistance.MaximumDistance = 2.999F;
    Require(!Editor::RaycastVoxelDocument(document,
        {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}}, shortDistance),
        "Maximum distance shorter than the hit must miss.");
    shortDistance.MaximumDistance = 3.0F;
    Require(Editor::RaycastVoxelDocument(document,
        {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}}, shortDistance).has_value(),
        "Exact maximum distance boundary must include the hit.");
    shortDistance.MaximumSteps = 1U;
    Require(!Editor::RaycastVoxelDocument(document,
        {{-2.0F, 1.5F, 1.5F}, {1.0F, 0.0F, 0.0F}}, shortDistance),
        "DDA iteration limit was not enforced.");

    Asset::Voxel::VoxelDocument boundary = Document({
        Model({1U, 1U, 1U}, {{0U, 0U, 0U, 4U}})});
    const auto outsideAdjacent = Editor::RaycastVoxelDocument(
        boundary, {{-1.0F, 0.5F, 0.5F}, {1.0F, 0.0F, 0.0F}});
    Require(outsideAdjacent && outsideAdjacent->AdjacentPosition ==
        Asset::Voxel::VoxelPosition{-1, 0, 0} &&
        !outsideAdjacent->AdjacentWithinBounds,
        "Adjacent position outside model bounds must remain explicit.");
    Require(!Editor::RaycastVoxelDocument(boundary,
        {{0.0F, 0.5F, 0.5F}, {-1.0F, 0.0F, 0.0F}}),
        "Ray leaving an exact minimum boundary must miss.");

    Asset::Voxel::VoxelDocument diagonal = Document({
        Model({3U, 3U, 3U}, {{1U, 1U, 1U, 7U}})});
    const auto edge = Editor::RaycastVoxelDocument(
        diagonal, {{-1.0F, -1.0F, 1.5F}, {1.0F, 1.0F, 0.0F}});
    const auto corner = Editor::RaycastVoxelDocument(
        diagonal, {{-1.0F, -1.0F, -1.0F}, {1.0F, 1.0F, 1.0F}});
    Require(edge && corner && edge->Coordinates ==
        Editor::VoxelCoordinates{1U, 1U, 1U} &&
        corner->Coordinates == Editor::VoxelCoordinates{1U, 1U, 1U} &&
        edge->Face == Editor::VoxelHitFace::NegativeX &&
        corner->Face == Editor::VoxelHitFace::NegativeX,
        "Edge/corner tie traversal or X/Y/Z face priority is incorrect.");

    Asset::Voxel::VoxelDocument empty;
    Require(!Editor::RaycastVoxelDocument(
        empty, {{}, {1.0F, 0.0F, 0.0F}}),
        "Empty document must miss safely.");
    const Editor::VoxelRaycastHit safeMiss{};
    Require(safeMiss.Face == Editor::VoxelHitFace::None &&
        safeMiss.Distance == 0.0F && Same(safeMiss.WorldPosition, {}) &&
        safeMiss.AdjacentPosition == Asset::Voxel::VoxelPosition{},
        "Default miss metadata is not deterministic.");
    Require(document.GetRevision() == revision && document.IsDirty() == dirty,
        "Ray picking modified the VoxelDocument.");
}

void TestTransformsAndMultipleModels()
{
    Asset::Voxel::VoxelDocument document = Document({
        Model({2U, 2U, 2U}, {{0U, 0U, 0U, 3U}}),
        Model({2U, 2U, 2U}, {{1U, 1U, 1U, 9U}})});
    Editor::VoxelRaycastOptions secondary;
    secondary.SubModelIndex = 1U;
    const auto secondaryHit = Editor::RaycastVoxelDocument(
        document, {{3.0F, 1.5F, 1.5F}, {-1.0F, 0.0F, 0.0F}}, secondary);
    Require(secondaryHit && secondaryHit->SubModelIndex == 1U &&
        secondaryHit->Coordinates == Editor::VoxelCoordinates{1U, 1U, 1U},
        "Explicit visible sub-model selection is incorrect.");

    const auto verifyTransform = [&document](
        const Editor::VoxelModelTransform& transform,
        const Editor::Vec3 localOrigin,
        const Editor::Vec3 localDirection,
        const Editor::Vec3 expectedWorldNormal)
    {
        Editor::VoxelRaycastOptions options;
        options.Transform = transform;
        const auto hit = Editor::RaycastVoxelDocument(
            document, WorldRayForLocal(transform, localOrigin, localDirection),
            options);
        Require(hit && hit->Coordinates == Editor::VoxelCoordinates{0U, 0U, 0U} &&
            Same(hit->Normal, expectedWorldNormal) &&
            Same(Editor::TransformPoint(
                transform.ModelMatrix, hit->LocalPosition), hit->WorldPosition),
            "Transformed voxel hit is inconsistent with model rendering space.");
    };

    const Editor::VoxelModelTransform identity{};
    verifyTransform(identity, {-1.0F, 0.5F, 0.5F}, {1.0F, 0.0F, 0.0F},
        {-1.0F, 0.0F, 0.0F});
    const Editor::Matrix4 translated =
        Editor::TranslationMatrix({10.0F, -3.0F, 2.0F});
    verifyTransform({translated, *Editor::InvertMatrix(translated)},
        {-1.0F, 0.5F, 0.5F}, {1.0F, 0.0F, 0.0F},
        {-1.0F, 0.0F, 0.0F});
    const Editor::Matrix4 rotated =
        Editor::RotationYMatrix(Editor::DegreesToRadians(90.0F));
    verifyTransform({rotated, *Editor::InvertMatrix(rotated)},
        {-1.0F, 0.5F, 0.5F}, {1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F});
    const Editor::Matrix4 scaled = Editor::UniformScaleMatrix(2.0F);
    verifyTransform({scaled, *Editor::InvertMatrix(scaled)},
        {-1.0F, 0.5F, 0.5F}, {1.0F, 0.0F, 0.0F},
        {-1.0F, 0.0F, 0.0F});
    const Editor::Matrix4 combined = Editor::MultiplyMatrix(
        translated, rotated);
    verifyTransform({combined, *Editor::InvertMatrix(combined)},
        {-1.0F, 0.5F, 0.5F}, {1.0F, 0.0F, 0.0F},
        {0.0F, 0.0F, 1.0F});

    const Editor::Matrix4 nonUniform{
        2.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 3.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 4.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 1.0F};
    verifyTransform({nonUniform, *Editor::InvertMatrix(nonUniform)},
        {-1.0F, 0.5F, 0.5F}, {1.0F, 0.0F, 0.0F},
        {-1.0F, 0.0F, 0.0F});

    Editor::VoxelRaycastOptions invalid;
    invalid.Transform.InverseModelMatrix = {};
    Require(!Editor::RaycastVoxelDocument(document,
        {{-1.0F, 0.5F, 0.5F}, {1.0F, 0.0F, 0.0F}}, invalid),
        "Non-invertible/inconsistent model transform must be rejected.");

    Asset::Voxel::VoxelDocument replacement = Document({
        Model({2U, 2U, 2U}, {{1U, 0U, 0U, 4U}})});
    Require(Editor::RaycastVoxelDocument(replacement,
        {{3.0F, 0.5F, 0.5F}, {-1.0F, 0.0F, 0.0F}})->Coordinates ==
            Editor::VoxelCoordinates{1U, 0U, 0U},
        "Replacement document did not produce its own nearest hit.");
}

void TestInteractionStateAndRevisionInvalidation()
{
    Editor::VoxelSelectionState state;
    Require(state.InteractionState() ==
        Editor::VoxelPickingInteractionState::Unavailable,
        "Picking state must start unavailable.");
    static_cast<void>(state.SetHovered(
        Editor::VoxelPickingInteractionState::OutsideViewport));
    Require(!state.Hovered() && state.InteractionState() ==
        Editor::VoxelPickingInteractionState::OutsideViewport,
        "Outside-viewport state is incorrect.");
    static_cast<void>(state.SetHovered(
        Editor::VoxelPickingInteractionState::NoDocument));
    static_cast<void>(state.SetHovered(
        Editor::VoxelPickingInteractionState::CameraInteraction));
    static_cast<void>(state.SetHovered(
        Editor::VoxelPickingInteractionState::Blocked));
    Require(!state.Hovered() && state.InteractionState() ==
        Editor::VoxelPickingInteractionState::Blocked,
        "Blocked picking must have no stale hit.");

    Asset::Voxel::VoxelDocument document = Document({
        Model({2U, 2U, 2U}, {{0U, 0U, 0U, 2U}})});
    const Editor::VoxelRay ray{{-1.0F, 0.5F, 0.5F}, {1.0F, 0.0F, 0.0F}};
    const auto hit = Editor::RaycastVoxelDocument(document, ray);
    Require(hit && state.SetHovered(
        Editor::VoxelPickingInteractionState::Hit, hit) && state.Hovered(),
        "Document hit did not enable hover/highlight state.");
    const std::uint64_t beforeRevision = document.GetRevision();
    Require(document.RemoveVoxel({0, 0, 0}).Changed &&
        document.GetRevision() == beforeRevision + 1U,
        "Revision invalidation fixture failed.");
    const auto miss = Editor::RaycastVoxelDocument(document, ray);
    Require(!miss && state.SetHovered(
        Editor::VoxelPickingInteractionState::NoHit, miss) &&
        !state.Hovered(),
        "Removed target did not clear previous hover immediately.");
    static_cast<void>(state.Clear());
    Require(state.InteractionState() ==
        Editor::VoxelPickingInteractionState::Unavailable,
        "Project/document close did not reset picking state.");

    Asset::Voxel::VoxelDocument large = Document({
        Model({128U, 128U, 128U}, {{127U, 64U, 64U, 5U}})});
    const auto largeHit = Editor::RaycastVoxelDocument(
        large, {{-1.0F, 64.5F, 64.5F}, {1.0F, 0.0F, 0.0F}});
    Require(largeHit && largeHit->Coordinates.X == 127U,
        "128-cubed qualitative traversal did not reach the target safely.");
}
}

int main()
{
    try
    {
        TestViewportRayBuilder();
        TestBoundsDdaAndResult();
        TestTransformsAndMultipleModels();
        TestInteractionStateAndRevisionInvalidation();
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
