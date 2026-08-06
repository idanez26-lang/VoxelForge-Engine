#include "ViewportRenderer.h"
#include "Preview/FacePlanGhostSurface.h"
#include "Transform/TransformPlacementPreviewAdapter.h"
#include "ViewportInteractionV2/ViewportPresentation.h"
#include "VoxelModelTransform.h"
#include "VoxelViewportState.h"

#include "VoxelForge/Renderer/Renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <fstream>
#include <limits>
#include <type_traits>
#include <vector>

namespace VoxelForge::Editor
{

namespace
{
struct GPUVertex final
{
    std::array<float, 3> Position{};
    std::array<float, 3> Normal{};
    std::array<float, 4> Color{};
};

static_assert(std::is_trivially_copyable_v<GPUVertex>);
static_assert(sizeof(GPUVertex) == 40U);
static_assert(offsetof(GPUVertex, Position) == 0U);
static_assert(offsetof(GPUVertex, Normal) == 12U);
static_assert(offsetof(GPUVertex, Color) == 24U);

struct GuideFace final
{
    std::array<float, 3> Normal{};
    std::array<std::array<float, 3>, 4> Corners{};
};

constexpr std::array<GuideFace, 6> GuideFaces{{
    {{-1.0F, 0.0F, 0.0F}, {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}}},
    {{1.0F, 0.0F, 0.0F}, {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}}},
    {{0.0F, -1.0F, 0.0F}, {{{0, 0, 1}, {0, 0, 0}, {1, 0, 0}, {1, 0, 1}}}},
    {{0.0F, 1.0F, 0.0F}, {{{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}}},
    {{0.0F, 0.0F, -1.0F}, {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}}},
    {{0.0F, 0.0F, 1.0F}, {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}}}
}};

constexpr float ViewportDepthClearValue = 1.0F;
constexpr std::int64_t ViewportStencilClearValue = 0;
static_assert(
    ViewportDepthClearValue >= 0.0F && ViewportDepthClearValue <= 1.0F);

[[nodiscard]] SDL_GPUTextureFormat ToSDLDepthFormat(
    const ViewportDepthFormat format) noexcept
{
    switch (format)
    {
    case ViewportDepthFormat::D32Float:
        return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
    case ViewportDepthFormat::D24Unorm:
        return SDL_GPU_TEXTUREFORMAT_D24_UNORM;
    case ViewportDepthFormat::D16Unorm:
        return SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    case ViewportDepthFormat::Unavailable:
    default:
        return SDL_GPU_TEXTUREFORMAT_INVALID;
    }
}

void AppendBox(
    std::vector<GPUVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const std::array<float, 3>& minimum,
    const std::array<float, 3>& maximum,
    const std::array<float, 4>& color)
{
    const auto appendFace = [&vertices, &indices, &minimum, &maximum, &color](
        const GuideFace& face)
    {
        const auto first = static_cast<std::uint32_t>(vertices.size());
        for (const auto& corner : face.Corners)
        {
            vertices.push_back({
                {minimum[0] + (maximum[0] - minimum[0]) * corner[0],
                 minimum[1] + (maximum[1] - minimum[1]) * corner[1],
                 minimum[2] + (maximum[2] - minimum[2]) * corner[2]},
                face.Normal,
                color});
        }
        constexpr std::array<std::uint32_t, 6> local{0U, 1U, 2U, 0U, 2U, 3U};
        for (const std::uint32_t index : local) indices.push_back(first + index);
    };
    for (const GuideFace& face : GuideFaces) appendFace(face);
}

void AppendBoxFace(
    std::vector<GPUVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const std::array<float, 3>& minimum,
    const std::array<float, 3>& maximum,
    const std::array<float, 4>& color,
    const FacePlanGhostSide side)
{
    const GuideFace& face = GuideFaces[static_cast<std::size_t>(side)];
    const auto first = static_cast<std::uint32_t>(vertices.size());
    for (const auto& corner : face.Corners)
    {
        const std::array<float, 3U> position =
            OffsetFacePlanGhostPointOutward({
                minimum[0] + (maximum[0] - minimum[0]) * corner[0],
                minimum[1] + (maximum[1] - minimum[1]) * corner[1],
                minimum[2] + (maximum[2] - minimum[2]) * corner[2]},
                side);
        vertices.push_back({
            position,
            face.Normal,
            color});
    }
    constexpr std::array<std::uint32_t, 6> local{
        0U, 1U, 2U, 0U, 2U, 3U};
    for (const std::uint32_t index : local)
        indices.push_back(first + index);
}

void AppendTriangle(
    std::vector<GPUVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const Vec3 a,
    const Vec3 b,
    const Vec3 c,
    const std::array<float, 4>& color)
{
    const Vec3 normal = Normalize(Cross(b - a, c - a));
    const auto first = static_cast<std::uint32_t>(vertices.size());
    for (const Vec3 point : {a, b, c})
        vertices.push_back({
            {point.X, point.Y, point.Z},
            {normal.X, normal.Y, normal.Z}, color});
    indices.insert(indices.end(), {first, first + 1U, first + 2U});
}

void AppendArrowHead(
    std::vector<GPUVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const TransformGizmoAxisView& axis,
    const std::array<float, 4>& color)
{
    if (!axis.HasArrowHead) return;
    for (std::size_t index = 0U; index < axis.ArrowBaseCorners.size(); ++index)
    {
        const Vec3 current = axis.ArrowBaseCorners[index];
        const Vec3 next = axis.ArrowBaseCorners[
            (index + 1U) % axis.ArrowBaseCorners.size()];
        AppendTriangle(vertices, indices, current, next, axis.End, color);
    }
    AppendTriangle(vertices, indices,
        axis.ArrowBaseCorners[0], axis.ArrowBaseCorners[2],
        axis.ArrowBaseCorners[1], color);
    AppendTriangle(vertices, indices,
        axis.ArrowBaseCorners[0], axis.ArrowBaseCorners[3],
        axis.ArrowBaseCorners[2], color);
}

void AppendVoxelOutline(
    std::vector<GPUVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const Asset::Voxel::VoxelPosition coordinates,
    const Vec3 center,
    const std::array<float, 4>& color)
{
    constexpr float expansion = 0.018F;
    constexpr float thickness = 0.035F;
    const Vec3 minimum = VoxelGridToViewport(
        {static_cast<float>(coordinates.X),
         static_cast<float>(coordinates.Y),
         static_cast<float>(coordinates.Z)}, center);
    const float x0 = minimum.X - expansion;
    const float y0 = minimum.Y - expansion;
    const float z0 = minimum.Z - expansion;
    const float x1 = minimum.X + 1.0F + expansion;
    const float y1 = minimum.Y + 1.0F + expansion;
    const float z1 = minimum.Z + 1.0F + expansion;
    for (const float y : {y0, y1})
        for (const float z : {z0, z1})
            AppendBox(vertices, indices,
                {x0, y - thickness, z - thickness},
                {x1, y + thickness, z + thickness}, color);
    for (const float x : {x0, x1})
        for (const float z : {z0, z1})
            AppendBox(vertices, indices,
                {x - thickness, y0, z - thickness},
                {x + thickness, y1, z + thickness}, color);
    for (const float x : {x0, x1})
        for (const float y : {y0, y1})
            AppendBox(vertices, indices,
                {x - thickness, y - thickness, z0},
                {x + thickness, y + thickness, z1}, color);
}

void AppendGhostVoxel(
    std::vector<GPUVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const GhostVoxel& ghost,
    const Vec3 center,
    const bool drawIndividualOutline)
{
    constexpr float expansion = 0.010F;
    const Vec3 minimum = VoxelGridToViewport(
        {static_cast<float>(ghost.Position.X),
         static_cast<float>(ghost.Position.Y),
         static_cast<float>(ghost.Position.Z)}, center);
    std::array<float, 4> fillColor = ghost.Color;
    fillColor[3] = std::clamp(ghost.Alpha, 0.0F, 1.0F);
    AppendBox(vertices, indices,
        {minimum.X - expansion, minimum.Y - expansion, minimum.Z - expansion},
        {minimum.X + 1.0F + expansion, minimum.Y + 1.0F + expansion,
         minimum.Z + 1.0F + expansion}, fillColor);

    if (!drawIndividualOutline) return;
    // Keep a fine, slightly stronger edge on the translucent volume so the
    // individual cells remain readable when a large brush overlaps the mesh.
    std::array<float, 4> outlineColor = ghost.Color;
    outlineColor[3] = std::clamp(ghost.Alpha + 0.20F, 0.0F, 1.0F);
    AppendVoxelOutline(vertices, indices, ghost.Position, center, outlineColor);
}

void AppendVoxelBoxOutline(
    std::vector<GPUVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const VoxelBoxBounds bounds,
    const Vec3 center,
    const std::array<float, 4>& color,
    const float expansion = 0.018F,
    const float thickness = 0.035F)
{
    const Vec3 minimum = VoxelGridToViewport(
        {static_cast<float>(bounds.Minimum.X),
         static_cast<float>(bounds.Minimum.Y),
         static_cast<float>(bounds.Minimum.Z)}, center);
    const float x0 = minimum.X - expansion;
    const float y0 = minimum.Y - expansion;
    const float z0 = minimum.Z - expansion;
    const float x1 = minimum.X +
        static_cast<float>(bounds.Maximum.X - bounds.Minimum.X + 1) + expansion;
    const float y1 = minimum.Y +
        static_cast<float>(bounds.Maximum.Y - bounds.Minimum.Y + 1) + expansion;
    const float z1 = minimum.Z +
        static_cast<float>(bounds.Maximum.Z - bounds.Minimum.Z + 1) + expansion;
    for (const float y : {y0, y1})
        for (const float z : {z0, z1})
            AppendBox(vertices, indices,
                {x0, y - thickness, z - thickness},
                {x1, y + thickness, z + thickness}, color);
    for (const float x : {x0, x1})
        for (const float z : {z0, z1})
            AppendBox(vertices, indices,
                {x - thickness, y0, z - thickness},
                {x + thickness, y1, z + thickness}, color);
    for (const float x : {x0, x1})
        for (const float y : {y0, y1})
            AppendBox(vertices, indices,
                {x - thickness, y - thickness, z0},
                {x + thickness, y + thickness, z1}, color);
}

void AppendTransformGizmo(
    std::vector<GPUVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const TransformGizmoView& gizmo,
    const float colorScale = 1.0F,
    const float alpha = 1.0F,
    const float thicknessScale = 1.0F,
    const bool centerOnly = false)
{
    if (!gizmo.Visible) return;
    // Rotate rings are screen-space anti-aliased polylines. Building each
    // chord as an axis-aligned 3D box creates square joints, overlap and long
    // projected extensions, especially without MSAA.
    if (gizmo.Mode == TransformGizmoMode::Rotate) return;
    const float centerRadius = gizmo.CenterRadius;
    const std::array<float, 4U> centerColor{
        0.92F * colorScale, 0.94F * colorScale,
        0.98F * colorScale, alpha};
    AppendBox(vertices, indices,
        {gizmo.Center.X - centerRadius, gizmo.Center.Y - centerRadius,
         gizmo.Center.Z - centerRadius},
        {gizmo.Center.X + centerRadius, gizmo.Center.Y + centerRadius,
         gizmo.Center.Z + centerRadius}, centerColor);
    for (const TransformGizmoAxisView& axis : gizmo.Axes)
    {
        if (centerOnly) break;
        if (!axis.Enabled) continue;
        const Vec3 shaftEnd = axis.HasArrowHead
            ? axis.ArrowBaseCenter : axis.End;
        const float thickness = axis.Thickness > 0.0F
            ? axis.Thickness * thicknessScale
            : gizmo.AxisThickness * thicknessScale;
        const std::array<float, 4U> color{
            axis.Color[0] * colorScale,
            axis.Color[1] * colorScale,
            axis.Color[2] * colorScale,
            alpha};
        AppendBox(vertices, indices,
            {std::min(axis.Start.X, shaftEnd.X) - thickness,
             std::min(axis.Start.Y, shaftEnd.Y) - thickness,
             std::min(axis.Start.Z, shaftEnd.Z) - thickness},
            {std::max(axis.Start.X, shaftEnd.X) + thickness,
             std::max(axis.Start.Y, shaftEnd.Y) + thickness,
             std::max(axis.Start.Z, shaftEnd.Z) + thickness},
            color);
        AppendArrowHead(vertices, indices, axis, color);
    }
}

void AppendSphereOutline(
    std::vector<GPUVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const VoxelSpherePreview sphere,
    const Vec3 modelCenter,
    const std::array<float, 4>& color)
{
    constexpr std::size_t segmentCount = 48U;
    constexpr float pi = 3.14159265358979323846F;
    constexpr float thickness = 0.035F;
    const Vec3 center = VoxelGridToViewport(
        {static_cast<float>(sphere.Center.X) + 0.5F,
         static_cast<float>(sphere.Center.Y) + 0.5F,
         static_cast<float>(sphere.Center.Z) + 0.5F}, modelCenter);
    const float radius = static_cast<float>(sphere.Radius) + 0.5F;
    const auto appendSegment = [&](const Vec3 start, const Vec3 end)
    {
        AppendBox(vertices, indices,
            {std::min(start.X, end.X) - thickness,
             std::min(start.Y, end.Y) - thickness,
             std::min(start.Z, end.Z) - thickness},
            {std::max(start.X, end.X) + thickness,
             std::max(start.Y, end.Y) + thickness,
             std::max(start.Z, end.Z) + thickness}, color);
    };
    const auto circlePoint = [center, radius](
        const std::size_t plane, const float angle)
    {
        const float cosine = std::cos(angle) * radius;
        const float sine = std::sin(angle) * radius;
        if (plane == 0U) return Vec3{center.X + cosine, center.Y + sine, center.Z};
        if (plane == 1U) return Vec3{center.X + cosine, center.Y, center.Z + sine};
        return Vec3{center.X, center.Y + cosine, center.Z + sine};
    };
    for (std::size_t plane = 0U; plane < 3U; ++plane)
        for (std::size_t segment = 0U; segment < segmentCount; ++segment)
        {
            const float angleA = 2.0F * pi * static_cast<float>(segment) /
                static_cast<float>(segmentCount);
            const float angleB = 2.0F * pi * static_cast<float>(segment + 1U) /
                static_cast<float>(segmentCount);
            appendSegment(
                circlePoint(plane, angleA), circlePoint(plane, angleB));
        }
}

Asset::Voxel::VoxelPosition ToVoxelPosition(
    const VoxelCoordinates coordinates) noexcept
{
    return {
        static_cast<std::int32_t>(coordinates.X),
        static_cast<std::int32_t>(coordinates.Y),
        static_cast<std::int32_t>(coordinates.Z)};
}

constexpr std::array<float, 4> ValidPlacementPreviewColor{
    0.18F, 1.0F, 0.32F, 1.0F};
constexpr std::array<float, 4> InvalidPlacementPreviewColor{
    1.0F, 0.15F, 0.12F, 1.0F};
constexpr std::array<float, 4> OccupiedPlacementPreviewColor{
    1.0F, 0.68F, 0.12F, 1.0F};
constexpr std::array<float, 4> EraserPlacementPreviewColor{
    1.0F, 0.24F, 0.05F, 1.0F};

std::vector<unsigned char> ReadBinary(const char* path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream)
    {
        return {};
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0)
    {
        return {};
    }
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(bytes.data()), size);
    return stream ? bytes : std::vector<unsigned char>{};
}

SDL_GPUShader* LoadShader(
    SDL_GPUDevice* device,
    const char* path,
    const SDL_GPUShaderStage stage,
    const std::uint32_t uniformBufferCount)
{
    const std::vector<unsigned char> code = ReadBinary(path);
    if (code.empty())
    {
        return nullptr;
    }
    SDL_GPUShaderCreateInfo createInfo{};
    createInfo.code = code.data();
    createInfo.code_size = code.size();
    createInfo.entrypoint = "main";
    createInfo.format = SDL_GPU_SHADERFORMAT_DXIL;
    createInfo.stage = stage;
    createInfo.num_uniform_buffers = uniformBufferCount;
    return SDL_CreateGPUShader(device, &createInfo);
}
}

struct ViewportRenderer::HighlightGeometryCache final
{
    std::vector<GPUVertex> Vertices;
    std::vector<std::uint32_t> Indices;
};

struct ViewportRenderer::TransformPreviewSnapshot final
{
    std::uint64_t Revision = 0U;
    bool DrawSourceGhost = true;
    VoxelPlacementPreview Placement;
    std::vector<Asset::Voxel::VoxelPosition> SourcePositions;
    SelectionBounds SourceBounds{};
    SelectionBounds PreviewBounds{};
    SelectionBounds CollisionBounds{};
    SelectionBounds OutOfBoundsBounds{};
    TransformPreviewRenderPlan Plan{};
};

ViewportRenderer::ViewportRenderer() = default;

ViewportRenderer::~ViewportRenderer()
{
    Shutdown();
}

bool ViewportRenderer::EnsurePipeline()
{
    if (pipeline_ != nullptr && smartBrushGhostPipeline_ != nullptr &&
        transformGizmoVisiblePipeline_ != nullptr &&
        transformGizmoOccludedPipeline_ != nullptr)
    {
        return true;
    }
    device_ = Renderer::Renderer::GetGPUDevice();
    if (device_ == nullptr)
    {
        SetError("No SDL GPU device is available.");
        return false;
    }
#if !defined(_WIN32)
    SetError("Voxel viewport v1 requires Windows and SDL GPU DXIL support.");
    return false;
#endif
    if (depthFormat_ == ViewportDepthFormat::Unavailable)
    {
        const auto supported = [this](const SDL_GPUTextureFormat format)
        {
            return SDL_GPUTextureSupportsFormat(device_, format,
                SDL_GPU_TEXTURETYPE_2D,
                SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
        };
        depthFormat_ = SelectViewportDepthFormat({
            supported(SDL_GPU_TEXTUREFORMAT_D32_FLOAT),
            supported(SDL_GPU_TEXTUREFORMAT_D24_UNORM),
            supported(SDL_GPU_TEXTUREFORMAT_D16_UNORM)});
        if (depthFormat_ == ViewportDepthFormat::Unavailable)
        {
            SetError("No supported viewport depth target format is available.");
            return false;
        }
    }
    const SDL_GPUTextureFormat depthFormat =
        ToSDLDepthFormat(depthFormat_);
    if ((SDL_GetGPUShaderFormats(device_) & SDL_GPU_SHADERFORMAT_DXIL) == 0)
    {
        SetError("Voxel viewport v1 requires SDL GPU DXIL shader support.");
        return false;
    }

    SDL_GPUShader* vertexShader = LoadShader(
        device_, VOXELFORGE_VIEWPORT_VERTEX_SHADER,
        SDL_GPU_SHADERSTAGE_VERTEX, 1U);
    SDL_GPUShader* fragmentShader = LoadShader(
        device_, VOXELFORGE_VIEWPORT_FRAGMENT_SHADER,
        SDL_GPU_SHADERSTAGE_FRAGMENT, 0U);
    if (vertexShader == nullptr || fragmentShader == nullptr)
    {
        if (vertexShader != nullptr) SDL_ReleaseGPUShader(device_, vertexShader);
        if (fragmentShader != nullptr) SDL_ReleaseGPUShader(device_, fragmentShader);
        SetError(std::string("Unable to load viewport shaders: ") + SDL_GetError());
        return false;
    }

    const SDL_GPUVertexBufferDescription bufferDescription{
        0U, sizeof(GPUVertex), SDL_GPU_VERTEXINPUTRATE_VERTEX, 0U};
    const std::array<SDL_GPUVertexAttribute, 3> attributes{{
        {0U, 0U, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GPUVertex, Position)},
        {1U, 0U, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GPUVertex, Normal)},
        {2U, 0U, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(GPUVertex, Color)}}};
    SDL_GPUColorTargetDescription colorDescription{
        SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, {}};
    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vertexShader;
    pipelineInfo.fragment_shader = fragmentShader;
    pipelineInfo.vertex_input_state = {
        &bufferDescription, 1U, attributes.data(),
        static_cast<std::uint32_t>(attributes.size())};
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    pipelineInfo.rasterizer_state.enable_depth_clip = true;
    pipelineInfo.target_info.color_target_descriptions = &colorDescription;
    pipelineInfo.target_info.num_color_targets = 1U;
    pipelineInfo.target_info.depth_stencil_format = depthFormat;
    pipelineInfo.target_info.has_depth_stencil_target = true;

    pipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipelineInfo.depth_stencil_state.enable_depth_test = true;
    pipelineInfo.depth_stencil_state.enable_depth_write = true;
    pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);

    // Ghost voxels are an editor overlay: depth-test them against the model,
    // but never write depth and blend their own alpha over the model pass.
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_BACK;
    pipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipelineInfo.depth_stencil_state.enable_depth_test = true;
    pipelineInfo.depth_stencil_state.enable_depth_write = false;
    colorDescription.blend_state.src_color_blendfactor =
        SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    colorDescription.blend_state.dst_color_blendfactor =
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorDescription.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colorDescription.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colorDescription.blend_state.dst_alpha_blendfactor =
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorDescription.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
    colorDescription.blend_state.enable_blend = true;
    smartBrushGhostPipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);

    // The regular visible gizmo remains an opaque editor overlay.
    colorDescription.blend_state.enable_blend = false;
    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipelineInfo.depth_stencil_state.enable_depth_test =
        TransformGizmoRenderPolicy::VisiblePassDepthTestEnabled;
    pipelineInfo.depth_stencil_state.enable_depth_write =
        TransformGizmoRenderPolicy::VisiblePassDepthWriteEnabled;
    transformGizmoVisiblePipeline_ =
        SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);

    colorDescription.blend_state.enable_blend = true;
    pipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_GREATER;
    pipelineInfo.depth_stencil_state.enable_depth_test =
        TransformGizmoRenderPolicy::OccludedPassDepthTestEnabled;
    pipelineInfo.depth_stencil_state.enable_depth_write =
        TransformGizmoRenderPolicy::OccludedPassDepthWriteEnabled;
    transformGizmoOccludedPipeline_ =
        SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);

    SDL_ReleaseGPUShader(device_, vertexShader);
    SDL_ReleaseGPUShader(device_, fragmentShader);
    if (pipeline_ == nullptr || smartBrushGhostPipeline_ == nullptr ||
        transformGizmoVisiblePipeline_ == nullptr ||
        transformGizmoOccludedPipeline_ == nullptr)
    {
        if (pipeline_ != nullptr)
            SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
        if (smartBrushGhostPipeline_ != nullptr)
            SDL_ReleaseGPUGraphicsPipeline(device_, smartBrushGhostPipeline_);
        if (transformGizmoVisiblePipeline_ != nullptr)
            SDL_ReleaseGPUGraphicsPipeline(
                device_, transformGizmoVisiblePipeline_);
        if (transformGizmoOccludedPipeline_ != nullptr)
            SDL_ReleaseGPUGraphicsPipeline(
                device_, transformGizmoOccludedPipeline_);
        pipeline_ = nullptr;
        smartBrushGhostPipeline_ = nullptr;
        transformGizmoVisiblePipeline_ = nullptr;
        transformGizmoOccludedPipeline_ = nullptr;
        SetError(std::string("Unable to create viewport pipelines: ") +
            SDL_GetError());
        return false;
    }
    return true;
}

bool ViewportRenderer::Upload(
    const Mesh::MeshData& mesh,
    const Voxel::VoxelPalette& palette,
    const Vec3 modelCenter)
{
    if (mesh.Empty())
    {
        ClearModel();
        ++modelUploadCount_;
        lastError_.clear();
        return true;
    }
    if (!UploadMesh(mesh, palette, modelCenter, vertexBuffer_, indexBuffer_,
            indexCount_, "voxel model"))
        return false;
    // The whole-mesh path owns the model view (VF-0262 lot 262-4).
    ReleaseModelChunks();
    ++modelUploadCount_;
    lastError_.clear();
    return true;
}

bool ViewportRenderer::UploadModelChunks(
    const std::span<const ModelChunkUpdate> updates,
    const Voxel::VoxelPalette& palette,
    const Vec3 modelCenter,
    const bool clearExisting)
{
    if (!EnsurePipeline()) return false;
    if (clearExisting) ReleaseModelChunks();
    // The chunked path owns the model view (VF-0262 lot 262-4).
    ReleaseWholeModelBuffers();
    for (const ModelChunkUpdate& update : updates)
    {
        if (update.Mesh == nullptr || update.Mesh->Empty())
        {
            const auto found = modelChunks_.find(update.Id);
            if (found != modelChunks_.end())
            {
                if (device_ != nullptr)
                {
                    if (found->second.VertexBuffer != nullptr)
                        SDL_ReleaseGPUBuffer(
                            device_, found->second.VertexBuffer);
                    if (found->second.IndexBuffer != nullptr)
                        SDL_ReleaseGPUBuffer(
                            device_, found->second.IndexBuffer);
                }
                modelChunks_.erase(found);
            }
            continue;
        }
        ModelChunkBuffers& slot = modelChunks_[update.Id];
        if (!UploadMesh(*update.Mesh, palette, modelCenter,
                slot.VertexBuffer, slot.IndexBuffer, slot.IndexCount,
                "voxel model chunk"))
        {
            // UploadMesh leaves the previous buffers intact on failure;
            // drop the slot only if it never held a mesh.
            if (slot.VertexBuffer == nullptr && slot.IndexBuffer == nullptr)
                modelChunks_.erase(update.Id);
            return false;
        }
    }
    if (modelChunks_.empty())
    {
        // An emptied model clears the whole model view, matching the
        // whole-mesh path's Upload(empty) behaviour.
        ClearModel();
    }
    ++modelUploadCount_;
    lastError_.clear();
    return true;
}

void ViewportRenderer::ReleaseModelChunks() noexcept
{
    if (device_ != nullptr)
    {
        for (auto& entry : modelChunks_)
        {
            if (entry.second.VertexBuffer != nullptr)
                SDL_ReleaseGPUBuffer(device_, entry.second.VertexBuffer);
            if (entry.second.IndexBuffer != nullptr)
                SDL_ReleaseGPUBuffer(device_, entry.second.IndexBuffer);
        }
    }
    modelChunks_.clear();
}

void ViewportRenderer::ReleaseWholeModelBuffers() noexcept
{
    if (device_ != nullptr)
    {
        if (vertexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, vertexBuffer_);
        if (indexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, indexBuffer_);
    }
    vertexBuffer_ = nullptr;
    indexBuffer_ = nullptr;
    indexCount_ = 0U;
}

bool ViewportRenderer::ConfigureExactPreviewMesh(
    const Mesh::MeshData* const mesh,
    const Voxel::VoxelPalette* const palette,
    const Vec3 modelCenter,
    const bool active,
    const std::uint64_t documentIdentity,
    const std::uint64_t documentRevision,
    const std::uint64_t planId,
    const std::uint64_t planRevision)
{
    if (!active)
    {
        ClearExactPreviewMesh();
        return true;
    }
    if (mesh == nullptr || palette == nullptr)
    {
        SetError("Exact preview requires prepared mesh and palette data.");
        return false;
    }
    if (exactPreviewActive_ && exactPreviewDocumentIdentity_ == documentIdentity &&
        exactPreviewDocumentRevision_ == documentRevision &&
        exactPreviewPlanId_ == planId && exactPreviewPlanRevision_ == planRevision)
    {
        return true;
    }
    if (mesh->Empty())
    {
        if (device_ != nullptr)
        {
            if (exactPreviewVertexBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device_, exactPreviewVertexBuffer_);
            if (exactPreviewIndexBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device_, exactPreviewIndexBuffer_);
        }
        exactPreviewVertexBuffer_ = nullptr;
        exactPreviewIndexBuffer_ = nullptr;
        exactPreviewIndexCount_ = 0U;
    }
    else if (!UploadMesh(*mesh, *palette, modelCenter, exactPreviewVertexBuffer_,
                 exactPreviewIndexBuffer_, exactPreviewIndexCount_,
                 "exact Smart Tool preview"))
    {
        // The old buffer represents another immutable plan. Never leave it on
        // screen when the replacement failed, otherwise preview and commit
        // could visibly diverge.
        ClearExactPreviewMesh();
        return false;
    }
    exactPreviewActive_ = true;
    exactPreviewDocumentIdentity_ = documentIdentity;
    exactPreviewDocumentRevision_ = documentRevision;
    exactPreviewPlanId_ = planId;
    exactPreviewPlanRevision_ = planRevision;
    lastError_.clear();
    return true;
}

bool ViewportRenderer::ConfigureExactPreviewChunks(
    const std::span<const ExactPreviewChunkUpdate> overrides,
    const Voxel::VoxelPalette& palette,
    const Vec3 modelCenter,
    const bool active,
    const std::uint64_t documentIdentity,
    const std::uint64_t documentRevision,
    const std::uint64_t compositionId)
{
    if (!active)
    {
        ReleaseExactPreviewChunks();
        return true;
    }
    if (!EnsurePipeline()) return false;
    if (modelChunks_.empty())
    {
        // Rien à surimprimer : le modèle n'est pas chunké (chargement, ou
        // chemin monolithique). Refuser est la seule réponse honnête — une
        // preview posée sur un modèle qui n'est pas là serait visiblement
        // fausse. L'appelant retombe sur la preview monolithique.
        SetError("Chunked exact preview requires a chunked model.");
        return false;
    }
    // Même géométrie qu'à l'appel précédent : aucun envoi. C'est ce
    // court-circuit qui rend un mouvement de souris gratuit quand le plan n'a
    // pas réellement changé.
    if (exactPreviewChunksActive_ &&
        exactPreviewDocumentIdentity_ == documentIdentity &&
        exactPreviewDocumentRevision_ == documentRevision &&
        exactPreviewChunksCompositionId_ == compositionId)
    {
        return true;
    }

    // La preview chunkée et la preview monolithique s'excluent, comme les deux
    // chemins du modèle. On relâche donc la monolithique — mais PAS nos propres
    // overrides : les relâcher pour les recréer aussitôt était le second poste
    // mesuré du décrochage, 10,1 ms de médiane sur ho-exact.
    ReleaseExactPreviewMonolithic();

    // LOT 4c : on patche la table en place. Les slots dont la révision est
    // inchangée gardent leurs tampons GPU et ne sont pas réenvoyés ; seuls les
    // chunks que le compositeur a réellement reconstruits paient un envoi.
    std::map<ModelChunkId, ModelChunkBuffers> retired = std::move(
        exactPreviewChunks_);
    exactPreviewChunks_.clear();
    const auto releaseSlot = [this](ModelChunkBuffers& slot) noexcept
    {
        if (device_ != nullptr)
        {
            if (slot.VertexBuffer != nullptr)
                SDL_ReleaseGPUBuffer(device_, slot.VertexBuffer);
            if (slot.IndexBuffer != nullptr)
                SDL_ReleaseGPUBuffer(device_, slot.IndexBuffer);
        }
        slot = {};
    };
    for (const ExactPreviewChunkUpdate& update : overrides)
    {
        const auto previous = retired.find(update.Id);
        ModelChunkBuffers slot{};
        if (previous != retired.end())
        {
            slot = previous->second;
            retired.erase(previous);
        }
        if (update.Mesh == nullptr || update.Mesh->Empty())
        {
            // Entrée vide DÉLIBÉRÉE : elle masque le chunk du modèle. Les
            // tampons éventuellement présents n'ont plus rien à décrire.
            releaseSlot(slot);
            exactPreviewChunks_[update.Id] = slot;
            continue;
        }
        // Même contenu qu'à l'envoi précédent : rien à faire. C'est ce test qui
        // rend un mouvement de souris gratuit pour les chunks intacts.
        if (update.Revision != 0U && slot.Revision == update.Revision &&
            slot.VertexBuffer != nullptr && slot.IndexBuffer != nullptr)
        {
            exactPreviewChunks_[update.Id] = slot;
            continue;
        }
        if (!UploadMesh(*update.Mesh, palette, modelCenter, slot.VertexBuffer,
                slot.IndexBuffer, slot.IndexCount,
                "exact Smart Tool preview chunk"))
        {
            // Ne jamais laisser une preview partielle à l'écran : elle
            // mélangerait deux états du document. Les slots encore en attente
            // de recyclage doivent aussi partir, sinon ils fuiraient.
            releaseSlot(slot);
            for (auto& entry : retired) releaseSlot(entry.second);
            ReleaseExactPreviewChunks();
            return false;
        }
        slot.Revision = update.Revision;
        exactPreviewChunks_[update.Id] = slot;
    }
    // Chunks qui ne sont plus surimprimés : leurs tampons n'ont plus d'objet.
    for (auto& entry : retired) releaseSlot(entry.second);
    exactPreviewChunksActive_ = true;
    exactPreviewDocumentIdentity_ = documentIdentity;
    exactPreviewDocumentRevision_ = documentRevision;
    exactPreviewChunksCompositionId_ = compositionId;
    lastError_.clear();
    return true;
}

void ViewportRenderer::ReleaseExactPreviewMonolithic() noexcept
{
    if (device_ != nullptr)
    {
        if (exactPreviewVertexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, exactPreviewVertexBuffer_);
        if (exactPreviewIndexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, exactPreviewIndexBuffer_);
    }
    exactPreviewVertexBuffer_ = nullptr;
    exactPreviewIndexBuffer_ = nullptr;
    exactPreviewIndexCount_ = 0U;
    exactPreviewActive_ = false;
    exactPreviewPlanId_ = 0U;
    exactPreviewPlanRevision_ = 0U;
}

void ViewportRenderer::ReleaseExactPreviewChunks() noexcept
{
    if (device_ != nullptr)
    {
        for (auto& entry : exactPreviewChunks_)
        {
            if (entry.second.VertexBuffer != nullptr)
                SDL_ReleaseGPUBuffer(device_, entry.second.VertexBuffer);
            if (entry.second.IndexBuffer != nullptr)
                SDL_ReleaseGPUBuffer(device_, entry.second.IndexBuffer);
        }
    }
    exactPreviewChunks_.clear();
    exactPreviewChunksActive_ = false;
    exactPreviewChunksCompositionId_ = 0U;
}

bool ViewportRenderer::UploadMesh(
    const Mesh::MeshData& mesh,
    const Voxel::VoxelPalette& palette,
    const Vec3 modelCenter,
    SDL_GPUBuffer*& vertexBuffer,
    SDL_GPUBuffer*& indexBuffer,
    std::uint32_t& indexCount,
    const std::string_view label)
{
    if (!EnsurePipeline() ||
        mesh.VertexCount() > std::numeric_limits<std::uint32_t>::max() ||
        mesh.IndexCount() > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }

    std::vector<GPUVertex> vertices;
    vertices.reserve(mesh.VertexCount());
    for (const Mesh::MeshVertex& source : mesh.Vertices())
    {
        const Voxel::VoxelColor* color = palette.Get(source.ColorIndex);
        const Voxel::VoxelColor fallback{255U, 0U, 255U, 255U};
        const Voxel::VoxelColor& value = color != nullptr ? *color : fallback;
        const Vec3 position = VoxelGridToViewport(
            {source.Position[0], source.Position[1], source.Position[2]},
            modelCenter);
        vertices.push_back({
            {position.X, position.Y, position.Z},
            source.Normal,
            ToViewportColor(value)});
    }

    const std::size_t vertexBytes = vertices.size() * sizeof(GPUVertex);
    const std::size_t indexBytes = mesh.Indices().size() * sizeof(std::uint32_t);
    if (vertexBytes + indexBytes > std::numeric_limits<std::uint32_t>::max())
    {
        SetError("Voxel mesh exceeds the SDL GPU upload size limit.");
        return false;
    }

    if (!UploadBufferPair(
            vertices.data(), vertexBytes,
            mesh.Indices().data(), indexBytes,
            vertexBuffer, indexBuffer, label))
    {
        return false;
    }
    indexCount = static_cast<std::uint32_t>(mesh.IndexCount());
    return true;
}

bool ViewportRenderer::UploadBufferPair(
    const void* vertexData,
    const std::size_t vertexBytes,
    const std::uint32_t* indexData,
    const std::size_t indexBytes,
    SDL_GPUBuffer*& vertexBuffer,
    SDL_GPUBuffer*& indexBuffer,
    const std::string_view label)
{
    const SDL_GPUBufferCreateInfo vertexInfo{
        SDL_GPU_BUFFERUSAGE_VERTEX, static_cast<std::uint32_t>(vertexBytes), 0U};
    const SDL_GPUBufferCreateInfo indexInfo{
        SDL_GPU_BUFFERUSAGE_INDEX, static_cast<std::uint32_t>(indexBytes), 0U};
    SDL_GPUBuffer* newVertexBuffer = SDL_CreateGPUBuffer(device_, &vertexInfo);
    SDL_GPUBuffer* newIndexBuffer = SDL_CreateGPUBuffer(device_, &indexInfo);
    const SDL_GPUTransferBufferCreateInfo transferInfo{
        SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        static_cast<std::uint32_t>(vertexBytes + indexBytes), 0U};
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
    const auto releaseNewBuffers = [this, &newVertexBuffer, &newIndexBuffer]()
    {
        if (newVertexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, newVertexBuffer);
        if (newIndexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, newIndexBuffer);
    };
    if (newVertexBuffer == nullptr || newIndexBuffer == nullptr || transfer == nullptr)
    {
        if (transfer != nullptr) SDL_ReleaseGPUTransferBuffer(device_, transfer);
        releaseNewBuffers();
        SetError("Unable to create " + std::string(label) +
            " GPU buffers: " + SDL_GetError());
        return false;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
    if (mapped == nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        releaseNewBuffers();
        SetError("Unable to map " + std::string(label) +
            " upload buffer: " + SDL_GetError());
        return false;
    }
    std::memcpy(mapped, vertexData, vertexBytes);
    std::memcpy(static_cast<unsigned char*>(mapped) + vertexBytes,
        indexData, indexBytes);
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device_);
    if (commandBuffer == nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        releaseNewBuffers();
        SetError("Unable to acquire " + std::string(label) +
            " upload command buffer: " + SDL_GetError());
        return false;
    }
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    if (copyPass == nullptr)
    {
        const std::string error = SDL_GetError();
        SDL_CancelGPUCommandBuffer(commandBuffer);
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        releaseNewBuffers();
        SetError("Unable to begin " + std::string(label) +
            " upload pass: " + error);
        return false;
    }
    const SDL_GPUTransferBufferLocation vertexSource{transfer, 0U};
    const SDL_GPUBufferRegion vertexDestination{
        newVertexBuffer, 0U, static_cast<std::uint32_t>(vertexBytes)};
    const SDL_GPUTransferBufferLocation indexSource{
        transfer, static_cast<std::uint32_t>(vertexBytes)};
    const SDL_GPUBufferRegion indexDestination{
        newIndexBuffer, 0U, static_cast<std::uint32_t>(indexBytes)};
    SDL_UploadToGPUBuffer(copyPass, &vertexSource, &vertexDestination, false);
    SDL_UploadToGPUBuffer(copyPass, &indexSource, &indexDestination, false);
    SDL_EndGPUCopyPass(copyPass);
    if (!SDL_SubmitGPUCommandBuffer(commandBuffer))
    {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        releaseNewBuffers();
        SetError("Unable to submit " + std::string(label) +
            " upload: " + SDL_GetError());
        return false;
    }
    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    if (vertexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, vertexBuffer);
    if (indexBuffer != nullptr) SDL_ReleaseGPUBuffer(device_, indexBuffer);
    vertexBuffer = newVertexBuffer;
    indexBuffer = newIndexBuffer;
    return true;
}

bool ViewportRenderer::UploadInteractionV2Highlights(
    const void* vertexData,
    const std::size_t vertexBytes,
    const std::uint32_t* indexData,
    const std::size_t indexBytes)
{
    const auto grow = [](const std::size_t current,
                         const std::size_t required) noexcept
    {
        std::size_t capacity = std::max<std::size_t>(current, 4096U);
        while (capacity < required &&
               capacity <= std::numeric_limits<std::size_t>::max() / 2U)
            capacity *= 2U;
        return std::max(capacity, required);
    };
    const std::size_t vertexCapacity = grow(
        interactionV2HighlightVertexCapacity_, vertexBytes);
    const std::size_t indexCapacity = grow(
        interactionV2HighlightIndexCapacity_, indexBytes);
    const std::size_t transferCapacity = grow(
        interactionV2HighlightTransferCapacity_, vertexBytes + indexBytes);
    if (vertexCapacity > std::numeric_limits<std::uint32_t>::max() ||
        indexCapacity > std::numeric_limits<std::uint32_t>::max() ||
        transferCapacity > std::numeric_limits<std::uint32_t>::max())
    {
        SetError("Viewport Interaction V2 highlight data is too large.");
        return false;
    }

    SDL_GPUBuffer* vertex = highlightVertexBuffer_;
    SDL_GPUBuffer* index = highlightIndexBuffer_;
    SDL_GPUTransferBuffer* transfer =
        interactionV2HighlightTransferBuffer_;
    const bool replaceVertex = vertex == nullptr ||
        vertexCapacity != interactionV2HighlightVertexCapacity_;
    const bool replaceIndex = index == nullptr ||
        indexCapacity != interactionV2HighlightIndexCapacity_;
    const bool replaceTransfer = transfer == nullptr ||
        transferCapacity != interactionV2HighlightTransferCapacity_;
    if (replaceVertex)
    {
        const SDL_GPUBufferCreateInfo info{
            SDL_GPU_BUFFERUSAGE_VERTEX,
            static_cast<std::uint32_t>(vertexCapacity), 0U};
        vertex = SDL_CreateGPUBuffer(device_, &info);
    }
    if (replaceIndex)
    {
        const SDL_GPUBufferCreateInfo info{
            SDL_GPU_BUFFERUSAGE_INDEX,
            static_cast<std::uint32_t>(indexCapacity), 0U};
        index = SDL_CreateGPUBuffer(device_, &info);
    }
    if (replaceTransfer)
    {
        const SDL_GPUTransferBufferCreateInfo info{
            SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            static_cast<std::uint32_t>(transferCapacity), 0U};
        transfer = SDL_CreateGPUTransferBuffer(device_, &info);
    }
    const auto releaseNew = [this, replaceVertex, replaceIndex,
                             replaceTransfer, vertex, index, transfer]()
    {
        if (replaceVertex && vertex != nullptr)
            SDL_ReleaseGPUBuffer(device_, vertex);
        if (replaceIndex && index != nullptr)
            SDL_ReleaseGPUBuffer(device_, index);
        if (replaceTransfer && transfer != nullptr)
            SDL_ReleaseGPUTransferBuffer(device_, transfer);
    };
    if (vertex == nullptr || index == nullptr || transfer == nullptr)
    {
        releaseNew();
        SetError(std::string(
            "Unable to create persistent V2 highlight buffers: ") +
            SDL_GetError());
        return false;
    }

    void* mapped = SDL_MapGPUTransferBuffer(
        device_, transfer, !replaceTransfer);
    if (mapped == nullptr)
    {
        releaseNew();
        SetError(std::string(
            "Unable to map persistent V2 highlight buffer: ") +
            SDL_GetError());
        return false;
    }
    std::memcpy(mapped, vertexData, vertexBytes);
    std::memcpy(static_cast<unsigned char*>(mapped) + vertexBytes,
        indexData, indexBytes);
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    SDL_GPUCommandBuffer* commandBuffer =
        SDL_AcquireGPUCommandBuffer(device_);
    if (commandBuffer == nullptr)
    {
        releaseNew();
        SetError(std::string(
            "Unable to acquire V2 highlight upload command buffer: ") +
            SDL_GetError());
        return false;
    }
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    if (copyPass == nullptr)
    {
        const std::string error = SDL_GetError();
        SDL_CancelGPUCommandBuffer(commandBuffer);
        releaseNew();
        SetError("Unable to begin V2 highlight upload pass: " + error);
        return false;
    }
    const SDL_GPUTransferBufferLocation vertexSource{transfer, 0U};
    const SDL_GPUBufferRegion vertexDestination{
        vertex, 0U, static_cast<std::uint32_t>(vertexBytes)};
    const SDL_GPUTransferBufferLocation indexSource{
        transfer, static_cast<std::uint32_t>(vertexBytes)};
    const SDL_GPUBufferRegion indexDestination{
        index, 0U, static_cast<std::uint32_t>(indexBytes)};
    SDL_UploadToGPUBuffer(
        copyPass, &vertexSource, &vertexDestination, !replaceVertex);
    SDL_UploadToGPUBuffer(
        copyPass, &indexSource, &indexDestination, !replaceIndex);
    SDL_EndGPUCopyPass(copyPass);
    if (!SDL_SubmitGPUCommandBuffer(commandBuffer))
    {
        releaseNew();
        SetError(std::string("Unable to submit V2 highlight upload: ") +
            SDL_GetError());
        return false;
    }

    if (replaceVertex)
    {
        if (highlightVertexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, highlightVertexBuffer_);
        highlightVertexBuffer_ = vertex;
        interactionV2HighlightVertexCapacity_ = vertexCapacity;
        ++interactionV2BufferRecreationCount_;
    }
    if (replaceIndex)
    {
        if (highlightIndexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, highlightIndexBuffer_);
        highlightIndexBuffer_ = index;
        interactionV2HighlightIndexCapacity_ = indexCapacity;
        ++interactionV2BufferRecreationCount_;
    }
    if (replaceTransfer)
    {
        if (interactionV2HighlightTransferBuffer_ != nullptr)
            SDL_ReleaseGPUTransferBuffer(
                device_, interactionV2HighlightTransferBuffer_);
        interactionV2HighlightTransferBuffer_ = transfer;
        interactionV2HighlightTransferCapacity_ = transferCapacity;
        ++interactionV2BufferRecreationCount_;
    }
    ++interactionV2UploadCount_;
    interactionV2UploadedBytes_ += vertexBytes + indexBytes;
    return true;
}

bool ViewportRenderer::UploadInteractionV2MoveSource(
    const void* vertexData,
    const std::size_t vertexBytes,
    const std::uint32_t* indexData,
    const std::size_t indexBytes)
{
    if (!UploadBufferPair(
            vertexData, vertexBytes, indexData, indexBytes,
            interactionV2MoveVertexBuffer_, interactionV2MoveIndexBuffer_,
            "Viewport Interaction V2 Move source"))
        return false;
    ++interactionV2MoveSourceUploadCount_;
    interactionV2MoveSourceUploadedBytes_ += vertexBytes + indexBytes;
    // UploadBufferPair creates one vertex, one index and one transient
    // transfer buffer for a changed selection source.
    interactionV2BufferRecreationCount_ += 3U;
    return true;
}

void ViewportRenderer::ConfigureGuides(
    const float width,
    const float height,
    const float depth) noexcept
{
    const float safeWidth = std::max(width, 0.0F);
    const float safeHeight = std::max(height, 0.0F);
    const float safeDepth = std::max(depth, 0.0F);
    if (guideWidth_ == safeWidth && guideHeight_ == safeHeight &&
        guideDepth_ == safeDepth)
    {
        return;
    }
    guideWidth_ = safeWidth;
    guideHeight_ = safeHeight;
    guideDepth_ = safeDepth;
    ReleaseGuides();
}

void ViewportRenderer::ConfigureHighlights(
    std::optional<VoxelCoordinates> hovered,
    const std::span<const Asset::Voxel::VoxelPosition> selected,
    std::optional<VoxelBoxBounds> selectionBounds,
    std::optional<SelectionBounds> editableSelectionBounds,
    const bool selectionToolStyle,
    const SelectionBoxVisualState selectionBoxVisualState,
    std::optional<Asset::Voxel::VoxelPosition> placementPreview,
    const VoxelPlacementPreviewStyle placementPreviewStyle,
    const std::span<const Asset::Voxel::VoxelPosition> brushPreview,
    const std::span<const Asset::Voxel::VoxelPosition> brushOccupiedPreview,
    std::optional<VoxelBoxBounds> brushAggregatePreview,
    std::optional<VoxelSpherePreview> brushAggregateSpherePreview,
    std::optional<VoxelBoxBounds> boxPreview,
    const std::span<const Asset::Voxel::VoxelPosition> linePreview,
    std::optional<VoxelSpherePreview> spherePreview,
    const VoxelPlacementPreview* smartBrushPreview,
    const SmartBrushGhostGeometryStyle smartBrushGhostGeometryStyle,
    const Vec3 modelCenter) noexcept
{
    const std::span<const VoxelPreviewInstance> smartBrushInstances =
        smartBrushPreview != nullptr
        ? smartBrushPreview->Instances()
        : std::span<const VoxelPreviewInstance>{};
    if (hovered)
    {
        const auto hoveredPosition = ToVoxelPosition(*hovered);
        if (std::find(selected.begin(), selected.end(), hoveredPosition) !=
            selected.end())
            hovered.reset();
    }
    const auto equals = [](
        const std::vector<Asset::Voxel::VoxelPosition>& stored,
        const std::span<const Asset::Voxel::VoxelPosition> incoming)
    {
        return stored.size() == incoming.size() &&
            std::equal(stored.begin(), stored.end(), incoming.begin());
    };
    if (hoveredHighlight_ == hovered && equals(selectedHighlights_, selected) &&
        selectionBoundsHighlight_ == selectionBounds &&
        editableSelectionBoundsHighlight_ == editableSelectionBounds &&
        selectionToolStyle_ == selectionToolStyle &&
        selectionBoxVisualState_ == selectionBoxVisualState &&
        placementPreviewHighlight_ == placementPreview &&
        placementPreviewStyle_ == placementPreviewStyle &&
        equals(brushPreviewHighlights_, brushPreview) &&
        equals(brushOccupiedPreviewHighlights_, brushOccupiedPreview) &&
        brushAggregatePreviewHighlight_ == brushAggregatePreview &&
        brushAggregateSpherePreviewHighlight_ == brushAggregateSpherePreview &&
        boxPreviewHighlight_ == boxPreview &&
        equals(linePreviewHighlights_, linePreview) &&
        spherePreviewHighlight_ == spherePreview &&
        smartBrushGhostGeometryStyle_ == smartBrushGhostGeometryStyle &&
        smartBrushGhostPreview_.size() == smartBrushInstances.size() &&
        std::equal(smartBrushGhostPreview_.begin(), smartBrushGhostPreview_.end(),
            smartBrushInstances.begin(), smartBrushInstances.end(),
            [](const GhostVoxel& stored, const VoxelPreviewInstance& incoming)
            {
                return stored.Position == incoming.Position &&
                    stored.Color == incoming.Color &&
                    stored.Alpha == incoming.Alpha;
            }) &&
        modelCenter_.X == modelCenter.X && modelCenter_.Y == modelCenter.Y &&
        modelCenter_.Z == modelCenter.Z)
    {
        return;
    }
    hoveredHighlight_ = hovered;
    selectedHighlights_.assign(selected.begin(), selected.end());
    selectionBoundsHighlight_ = selectionBounds;
    editableSelectionBoundsHighlight_ = editableSelectionBounds;
    selectionToolStyle_ = selectionToolStyle;
    selectionBoxVisualState_ = selectionBoxVisualState;
    placementPreviewHighlight_ = placementPreview;
    placementPreviewStyle_ = placementPreviewStyle;
    brushPreviewHighlights_.assign(brushPreview.begin(), brushPreview.end());
    brushOccupiedPreviewHighlights_.assign(
        brushOccupiedPreview.begin(), brushOccupiedPreview.end());
    brushAggregatePreviewHighlight_ = brushAggregatePreview;
    brushAggregateSpherePreviewHighlight_ = brushAggregateSpherePreview;
    boxPreviewHighlight_ = boxPreview;
    linePreviewHighlights_.assign(linePreview.begin(), linePreview.end());
    spherePreviewHighlight_ = spherePreview;
    smartBrushGhostGeometryStyle_ = smartBrushGhostGeometryStyle;
    smartBrushGhostPreview_.clear();
    smartBrushGhostPreview_.reserve(smartBrushInstances.size());
    for (const VoxelPreviewInstance& instance : smartBrushInstances)
        smartBrushGhostPreview_.push_back({instance.Position,
            GhostVoxelState::Added, instance.Color, instance.Alpha});
    modelCenter_ = modelCenter;
    highlightsDirty_ = hoveredHighlight_.has_value() ||
        !selectedHighlights_.empty() ||
        selectionBoundsHighlight_.has_value() ||
        editableSelectionBoundsHighlight_.has_value() ||
        placementPreviewHighlight_.has_value() ||
        !brushPreviewHighlights_.empty() ||
        !brushOccupiedPreviewHighlights_.empty() ||
        brushAggregatePreviewHighlight_.has_value() ||
        brushAggregateSpherePreviewHighlight_.has_value() ||
        boxPreviewHighlight_.has_value() || !linePreviewHighlights_.empty() ||
        spherePreviewHighlight_.has_value() || !smartBrushGhostPreview_.empty() ||
        !voxelPreviewGhosts_.empty() ||
        transformPreview_ != nullptr ||
        transformGizmo_.has_value();
    if (!highlightsDirty_) ReleaseHighlights();
}

void ViewportRenderer::ConfigureTransformPreview(
    const TransformPreviewRenderData* preview) noexcept
{
    if (preview == nullptr)
    {
        if (!transformPreview_) return;
        transformPreview_.reset();
    }
    else
    {
        if (transformPreview_ &&
            transformPreview_->Revision == preview->Revision)
            return;
        if (!transformPreview_)
            transformPreview_ = std::make_unique<TransformPreviewSnapshot>();
        TransformPreviewSnapshot& snapshot = *transformPreview_;
        snapshot.Revision = preview->Revision;
        snapshot.DrawSourceGhost = preview->DrawSourceGhost;
        snapshot.Placement = BuildTransformPlacementPreview(*preview);
        snapshot.SourcePositions.assign(
            preview->SourcePositions.begin(), preview->SourcePositions.end());
        snapshot.SourceBounds = preview->SourceBounds;
        snapshot.PreviewBounds = preview->PreviewBounds;
        snapshot.CollisionBounds = preview->CollisionBounds;
        snapshot.OutOfBoundsBounds = preview->OutOfBoundsBounds;
        snapshot.Plan = preview->Plan;
    }
    highlightsDirty_ = hoveredHighlight_.has_value() ||
        !selectedHighlights_.empty() ||
        selectionBoundsHighlight_.has_value() ||
        editableSelectionBoundsHighlight_.has_value() ||
        placementPreviewHighlight_.has_value() ||
        boxPreviewHighlight_.has_value() || !linePreviewHighlights_.empty() ||
        spherePreviewHighlight_.has_value() || !smartBrushGhostPreview_.empty() ||
        !voxelPreviewGhosts_.empty() ||
        transformPreview_ != nullptr ||
        transformGizmo_.has_value();
    if (!highlightsDirty_) ReleaseHighlights();
}

void ViewportRenderer::ConfigureInteractionV2(
    const std::span<const Asset::Voxel::VoxelPosition> selectedDetail,
    const std::optional<SelectionBounds> selectionBounds,
    const InteractionV2::MovePreviewPresentation* movePreview,
    const Vec3 modelCenter,
    const std::uint64_t presentationRevision,
    const bool active) noexcept
{
    if (!active)
    {
        if (interactionV2Revision_ == 0U) return;
        interactionV2Revision_ = 0U;
        selectedHighlights_.clear();
        editableSelectionBoundsHighlight_.reset();
        selectionBoundsHighlight_.reset();
        transformPreview_.reset();
        transformGizmo_.reset();
        interactionV2MoveActive_ = false;
        interactionV2MoveSourceIdentity_ = 0U;
        ReleaseInteractionV2MoveSource();
        highlightsDirty_ = true;
        return;
    }
    if (interactionV2Revision_ == presentationRevision) return;
    interactionV2Revision_ = presentationRevision;
    hoveredHighlight_.reset();
    selectedHighlights_.assign(
        selectedDetail.begin(), selectedDetail.end());
    editableSelectionBoundsHighlight_ = selectionBounds;
    selectionBoundsHighlight_.reset();
    selectionToolStyle_ = true;
    selectionBoxVisualState_ =
        movePreview == nullptr ? SelectionBoxVisualState::Normal
        : movePreview->Validation ==
            InteractionV2::MoveValidationState::Deferred
            ? SelectionBoxVisualState::MovingPending
        : movePreview->Validation ==
                InteractionV2::MoveValidationState::Valid
            ? SelectionBoxVisualState::Moving
            : SelectionBoxVisualState::MovingInvalid;
    placementPreviewHighlight_.reset();
    brushPreviewHighlights_.clear();
    brushOccupiedPreviewHighlights_.clear();
    brushAggregatePreviewHighlight_.reset();
    brushAggregateSpherePreviewHighlight_.reset();
    boxPreviewHighlight_.reset();
    linePreviewHighlights_.clear();
    spherePreviewHighlight_.reset();
    smartBrushGhostPreview_.clear();
    voxelPreviewGhosts_.clear();
    transformGizmo_.reset();
    modelCenter_ = modelCenter;

    transformPreview_.reset();
    interactionV2MoveActive_ = movePreview != nullptr;
    if (movePreview == nullptr)
    {
        interactionV2MoveDelta_ = {};
    }
    else
    {
        if (interactionV2MoveDelta_ != movePreview->Delta)
        {
            interactionV2MoveDelta_ = movePreview->Delta;
            ++interactionV2MoveDeltaUpdateCount_;
        }
        if (interactionV2MoveSourceIdentity_ !=
            movePreview->SourceIdentity)
        {
            interactionV2MoveSourceIdentity_ =
                movePreview->SourceIdentity;
            if (!interactionV2MoveGeometry_)
                interactionV2MoveGeometry_ =
                    std::make_unique<HighlightGeometryCache>();
            auto& vertices = interactionV2MoveGeometry_->Vertices;
            auto& indices = interactionV2MoveGeometry_->Indices;
            vertices.clear();
            indices.clear();
            if (movePreview->SourcePositions.size() <=
                TransformPreviewRenderPolicy::IndividualVoxelLimit)
            {
                constexpr std::array<float, 4> moveColor{
                    0.18F, 0.86F, 1.0F, 1.0F};
                constexpr std::size_t boxesPerOutline = 12U;
                vertices.reserve(
                    movePreview->SourcePositions.size() *
                    boxesPerOutline * 24U);
                indices.reserve(
                    movePreview->SourcePositions.size() *
                    boxesPerOutline * 36U);
                for (const auto position : movePreview->SourcePositions)
                    AppendVoxelOutline(
                        vertices, indices, position, modelCenter, moveColor);
            }
            interactionV2MoveSourceDirty_ = true;
        }
    }
    highlightsDirty_ = !selectedHighlights_.empty() ||
        editableSelectionBoundsHighlight_.has_value() ||
        interactionV2MoveActive_;
    if (!highlightsDirty_) ReleaseHighlights();
}

void ViewportRenderer::ConfigureVoxelPreview(const VoxelPreviewData* preview) noexcept
{
    ConfigureVoxelPlacementPreview(
        preview != nullptr ? &preview->Placement : nullptr);
}

void ViewportRenderer::ConfigureVoxelPlacementPreview(
    const VoxelPlacementPreview* preview) noexcept
{
    if (preview == nullptr || !preview->IsActive())
    {
        if (voxelPreviewGhosts_.empty()) return;
        voxelPreviewGhosts_.clear();
        voxelPreviewRevision_ = 0U;
        highlightsDirty_ = true;
        return;
    }
    if (!voxelPreviewGhosts_.empty() &&
        voxelPreviewRevision_ == preview->Revision())
        return;
    try
    {
        voxelPreviewGhosts_.clear();
        voxelPreviewGhosts_.reserve(preview->Instances().size());
        for (const VoxelPreviewInstance& instance : preview->Instances())
            voxelPreviewGhosts_.push_back({instance.Position,
                GhostVoxelState::Added, instance.Color, instance.Alpha});
        voxelPreviewRevision_ = preview->Revision();
        highlightsDirty_ = true;
    }
    catch (const std::bad_alloc&)
    {
        voxelPreviewGhosts_.clear();
        voxelPreviewRevision_ = 0U;
        highlightsDirty_ = true;
    }
}

void ViewportRenderer::ConfigureTransformGizmo(
    const TransformGizmoView* gizmo) noexcept
{
    const std::optional<TransformGizmoView> next =
        gizmo != nullptr && gizmo->Visible
        ? std::optional<TransformGizmoView>(*gizmo)
        : std::nullopt;
    if (transformGizmo_ == next) return;
    transformGizmo_ = next;
    highlightsDirty_ = hoveredHighlight_.has_value() ||
        !selectedHighlights_.empty() ||
        selectionBoundsHighlight_.has_value() ||
        editableSelectionBoundsHighlight_.has_value() ||
        placementPreviewHighlight_.has_value() ||
        boxPreviewHighlight_.has_value() || !linePreviewHighlights_.empty() ||
        spherePreviewHighlight_.has_value() || !smartBrushGhostPreview_.empty() ||
        !voxelPreviewGhosts_.empty() ||
        transformPreview_ != nullptr ||
        transformGizmo_.has_value();
    if (!highlightsDirty_) ReleaseHighlights();
}

bool ViewportRenderer::EnsureHighlights()
{
    if (!highlightsDirty_) return true;
    if (interactionV2MoveSourceDirty_)
    {
        if (!interactionV2MoveGeometry_ ||
            interactionV2MoveGeometry_->Indices.empty())
        {
            ReleaseInteractionV2MoveSource();
        }
        else if (!UploadInteractionV2MoveSource(
            interactionV2MoveGeometry_->Vertices.data(),
            interactionV2MoveGeometry_->Vertices.size() *
                sizeof(GPUVertex),
            interactionV2MoveGeometry_->Indices.data(),
            interactionV2MoveGeometry_->Indices.size() *
                sizeof(std::uint32_t)))
        {
            return false;
        }
        else
        {
            interactionV2MoveIndexCount_ =
                static_cast<std::uint32_t>(
                    interactionV2MoveGeometry_->Indices.size());
        }
        interactionV2MoveSourceDirty_ = false;
    }
    if (!highlightGeometry_)
        highlightGeometry_ = std::make_unique<HighlightGeometryCache>();
    std::vector<GPUVertex>& vertices = highlightGeometry_->Vertices;
    std::vector<std::uint32_t>& indices = highlightGeometry_->Indices;
    vertices.clear();
    indices.clear();
    const std::size_t outlineCount =
        static_cast<std::size_t>(placementPreviewHighlight_.has_value()) +
        brushPreviewHighlights_.size() +
        brushOccupiedPreviewHighlights_.size() +
        static_cast<std::size_t>(brushAggregatePreviewHighlight_.has_value()) +
        static_cast<std::size_t>(boxPreviewHighlight_.has_value()) +
        linePreviewHighlights_.size() +
        static_cast<std::size_t>(hoveredHighlight_.has_value()) +
        selectedHighlights_.size() +
        static_cast<std::size_t>(selectionBoundsHighlight_.has_value()) +
        static_cast<std::size_t>(editableSelectionBoundsHighlight_.has_value());
    std::size_t transformOutlineCount = 0U;
    if (transformPreview_)
    {
        transformOutlineCount = 2U;
        if (transformPreview_->Plan.DrawIndividualVoxels)
            transformOutlineCount += transformPreview_->SourcePositions.size() +
                transformPreview_->Placement.Instances().size();
        else if (transformPreview_->Plan.DrawIndividualCollisions)
            transformOutlineCount +=
                transformPreview_->Plan.CollisionVoxelCount +
                transformPreview_->Plan.OutOfBoundsVoxelCount;
        else
            transformOutlineCount +=
                static_cast<std::size_t>(
                    transformPreview_->CollisionBounds.Valid) +
                static_cast<std::size_t>(
                    transformPreview_->OutOfBoundsBounds.Valid);
    }
    constexpr std::size_t boxesPerOutline = 12U;
    constexpr std::size_t sphereBoxCount = 3U * 48U;
    const std::size_t boxCount =
        (outlineCount + transformOutlineCount) * boxesPerOutline +
        (spherePreviewHighlight_ ? sphereBoxCount : 0U) +
        (brushAggregateSpherePreviewHighlight_ ? sphereBoxCount : 0U);
    vertices.reserve(boxCount * 24U);
    indices.reserve(boxCount * 36U);
    if (placementPreviewHighlight_)
        AppendVoxelOutline(
            vertices, indices, *placementPreviewHighlight_, modelCenter_,
            placementPreviewStyle_ == VoxelPlacementPreviewStyle::PencilValid
                ? ValidPlacementPreviewColor
                : placementPreviewStyle_ ==
                    VoxelPlacementPreviewStyle::Eraser
                ? EraserPlacementPreviewColor
                : placementPreviewStyle_ ==
                    VoxelPlacementPreviewStyle::PencilOccupied
                ? OccupiedPlacementPreviewColor
                : InvalidPlacementPreviewColor);
    const std::array<float, 4>& brushPreviewColor =
        placementPreviewStyle_ == VoxelPlacementPreviewStyle::PencilValid
        ? ValidPlacementPreviewColor
        : InvalidPlacementPreviewColor;
    for (const Asset::Voxel::VoxelPosition position : brushPreviewHighlights_)
        AppendVoxelOutline(
            vertices, indices, position, modelCenter_, brushPreviewColor);
    for (const Asset::Voxel::VoxelPosition position :
         brushOccupiedPreviewHighlights_)
    {
        AppendVoxelOutline(
            vertices, indices, position, modelCenter_,
            OccupiedPlacementPreviewColor);
    }
    if (brushAggregatePreviewHighlight_)
    {
        AppendVoxelBoxOutline(
            vertices, indices, *brushAggregatePreviewHighlight_, modelCenter_,
            placementPreviewStyle_ == VoxelPlacementPreviewStyle::PencilOccupied
                ? OccupiedPlacementPreviewColor
                : brushPreviewColor);
    }
    if (brushAggregateSpherePreviewHighlight_)
    {
        AppendSphereOutline(
            vertices, indices, *brushAggregateSpherePreviewHighlight_,
            modelCenter_,
            placementPreviewStyle_ == VoxelPlacementPreviewStyle::PencilOccupied
                ? OccupiedPlacementPreviewColor
                : brushPreviewColor);
    }
    if (boxPreviewHighlight_)
        AppendVoxelBoxOutline(
            vertices, indices, *boxPreviewHighlight_, modelCenter_,
            ValidPlacementPreviewColor);
    for (const Asset::Voxel::VoxelPosition position : linePreviewHighlights_)
        AppendVoxelOutline(
            vertices, indices, position, modelCenter_,
            ValidPlacementPreviewColor);
    if (spherePreviewHighlight_)
        AppendSphereOutline(vertices, indices, *spherePreviewHighlight_,
            modelCenter_, ValidPlacementPreviewColor);
    if (hoveredHighlight_)
        AppendVoxelOutline(vertices, indices,
            ToVoxelPosition(*hoveredHighlight_), modelCenter_,
            {1.0F, 0.88F, 0.12F, 1.0F});
    for (const auto position : selectedHighlights_)
        AppendVoxelOutline(vertices, indices, position, modelCenter_,
            selectionToolStyle_
                ? std::array<float, 4>{0.12F, 0.72F, 1.0F, 1.0F}
                : std::array<float, 4>{1.0F, 0.38F, 0.08F, 1.0F});
    if (selectionBoundsHighlight_ && selectedHighlights_.size() > 1U)
        AppendVoxelBoxOutline(vertices, indices, *selectionBoundsHighlight_,
            modelCenter_, {0.18F, 0.82F, 1.0F, 0.82F});
    if (editableSelectionBoundsHighlight_)
    {
        const SelectionBounds& bounds = *editableSelectionBoundsHighlight_;
        const bool moving = selectionBoxVisualState_ ==
            SelectionBoxVisualState::Moving;
        const bool movingPending = selectionBoxVisualState_ ==
            SelectionBoxVisualState::MovingPending;
        const bool movingInvalid = selectionBoxVisualState_ ==
            SelectionBoxVisualState::MovingInvalid;
        const bool hovered = selectionBoxVisualState_ ==
            SelectionBoxVisualState::Hovered;
        AppendVoxelBoxOutline(vertices, indices,
            {bounds.Minimum, bounds.Maximum}, modelCenter_,
            movingInvalid
                ? std::array<float, 4>{1.0F, 0.18F, 0.12F, 1.0F}
            : movingPending
                ? std::array<float, 4>{1.0F, 0.64F, 0.10F, 1.0F}
            : moving
                ? std::array<float, 4>{0.22F, 1.0F, 0.84F, 1.0F}
                : hovered
                ? std::array<float, 4>{0.12F, 1.0F, 0.78F, 1.0F}
                : std::array<float, 4>{0.08F, 0.98F, 0.72F, 1.0F},
            (moving || movingPending || movingInvalid)
                ? 0.080F : hovered ? 0.072F : 0.065F,
            (moving || movingPending || movingInvalid)
                ? 0.085F : hovered ? 0.078F : 0.070F);
    }
    if (transformPreview_)
    {
        constexpr std::array<float, 4> sourceGhostColor{
            0.42F, 0.52F, 0.62F, 1.0F};
        constexpr std::array<float, 4> destinationBoundsColor{
            0.18F, 0.86F, 1.0F, 1.0F};
        constexpr std::array<float, 4> collisionColor{
            1.0F, 0.16F, 0.10F, 1.0F};
        constexpr std::array<float, 4> outOfBoundsColor{
            1.0F, 0.56F, 0.08F, 1.0F};
        const TransformPreviewSnapshot& preview = *transformPreview_;
        if (preview.DrawSourceGhost && preview.SourceBounds.Valid)
            AppendVoxelBoxOutline(vertices, indices,
                {preview.SourceBounds.Minimum, preview.SourceBounds.Maximum},
                modelCenter_, sourceGhostColor, 0.030F, 0.028F);
        if (preview.PreviewBounds.Valid)
            AppendVoxelBoxOutline(vertices, indices,
                {preview.PreviewBounds.Minimum, preview.PreviewBounds.Maximum},
                modelCenter_, destinationBoundsColor, 0.045F, 0.042F);

        if (preview.Plan.DrawIndividualVoxels)
        {
            if (preview.DrawSourceGhost)
                for (const Asset::Voxel::VoxelPosition position :
                     preview.SourcePositions)
                    AppendVoxelOutline(vertices, indices, position,
                        modelCenter_, sourceGhostColor);
            for (const VoxelPreviewInstance& instance :
                 preview.Placement.Instances())
                AppendVoxelOutline(vertices, indices, instance.Position,
                    modelCenter_, instance.Color);
        }
        else if (preview.Plan.DrawIndividualCollisions)
        {
            for (const VoxelPreviewInstance& instance :
                 preview.Placement.Instances())
            {
                if (instance.Semantic == VoxelPreviewSemantic::Overlap ||
                    instance.Semantic == VoxelPreviewSemantic::Invalid)
                    AppendVoxelOutline(vertices, indices,
                        instance.Position, modelCenter_, instance.Color);
            }
        }
        else
        {
            if (preview.CollisionBounds.Valid)
                AppendVoxelBoxOutline(vertices, indices,
                    {preview.CollisionBounds.Minimum,
                     preview.CollisionBounds.Maximum},
                    modelCenter_, collisionColor, 0.060F, 0.055F);
            if (preview.OutOfBoundsBounds.Valid)
                AppendVoxelBoxOutline(vertices, indices,
                    {preview.OutOfBoundsBounds.Minimum,
                     preview.OutOfBoundsBounds.Maximum},
                    modelCenter_, outOfBoundsColor, 0.060F, 0.055F);
        }
    }
    if (indices.empty())
    {
        if (interactionV2Revision_ == 0U && device_ != nullptr)
        {
            if (highlightVertexBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device_, highlightVertexBuffer_);
            if (highlightIndexBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device_, highlightIndexBuffer_);
            if (interactionV2HighlightTransferBuffer_ != nullptr)
                SDL_ReleaseGPUTransferBuffer(
                    device_, interactionV2HighlightTransferBuffer_);
        }
        if (interactionV2Revision_ == 0U)
        {
            highlightVertexBuffer_ = nullptr;
            highlightIndexBuffer_ = nullptr;
            interactionV2HighlightTransferBuffer_ = nullptr;
            interactionV2HighlightVertexCapacity_ = 0U;
            interactionV2HighlightIndexCapacity_ = 0U;
            interactionV2HighlightTransferCapacity_ = 0U;
        }
        highlightIndexCount_ = 0U;
    }
    else if (!(interactionV2Revision_ != 0U
            ? UploadInteractionV2Highlights(
                vertices.data(), vertices.size() * sizeof(GPUVertex),
                indices.data(), indices.size() * sizeof(std::uint32_t))
            : UploadBufferPair(
                vertices.data(), vertices.size() * sizeof(GPUVertex),
                indices.data(), indices.size() * sizeof(std::uint32_t),
                highlightVertexBuffer_, highlightIndexBuffer_,
                "voxel selection highlights")))
    {
        return false;
    }
    else
    {
        highlightIndexCount_ = static_cast<std::uint32_t>(indices.size());
    }

    if (!smartBrushGhostGeometry_)
        smartBrushGhostGeometry_ = std::make_unique<HighlightGeometryCache>();
    std::vector<GPUVertex>& ghostVertices = smartBrushGhostGeometry_->Vertices;
    std::vector<std::uint32_t>& ghostIndices = smartBrushGhostGeometry_->Indices;
    ghostVertices.clear();
    ghostIndices.clear();
    // An individual outline is useful for a normal brush. For a very large
    // brush, retain every filled ghost cell but replace thousands of repeated
    // edge boxes with one aggregate outline around the rendered coordinates.
    constexpr std::size_t IndividualGhostOutlineLimit = 128U;
    const std::size_t ghostPreviewCount = smartBrushGhostPreview_.size() +
        voxelPreviewGhosts_.size();
    const bool drawIndividualGhostOutlines =
        smartBrushGhostGeometryStyle_ ==
            SmartBrushGhostGeometryStyle::VoxelBoxes &&
        ghostPreviewCount <= IndividualGhostOutlineLimit;
    const bool drawIndividualVoxelPreviewOutlines =
        voxelPreviewGhosts_.size() <= IndividualGhostOutlineLimit;
    if (smartBrushGhostGeometryStyle_ ==
        SmartBrushGhostGeometryStyle::ExposedFaceSurface)
    {
        const FacePlanGhostSurface surface =
            BuildFacePlanGhostSurface(smartBrushGhostPreview_);
        ghostVertices.reserve(
            ghostVertices.size() + surface.ExposedFaceCount * 4U);
        ghostIndices.reserve(
            ghostIndices.size() + surface.ExposedFaceCount * 6U);
        for (const FacePlanGhostSurfaceCell& cell : surface.Cells)
        {
            const GhostVoxel& ghost =
                smartBrushGhostPreview_[cell.GhostIndex];
            const FacePlanGhostVoxelBounds voxelBounds =
                MakeFacePlanGhostVoxelBounds(ghost.Position);
            std::array<float, 4> fillColor = ghost.Color;
            fillColor[3] = std::clamp(ghost.Alpha, 0.0F, 1.0F);
            const std::array<float, 3> minimum{
                voxelBounds.Minimum[0] - modelCenter_.X,
                voxelBounds.Minimum[1] - modelCenter_.Y,
                voxelBounds.Minimum[2] - modelCenter_.Z};
            const std::array<float, 3> maximum{
                voxelBounds.Maximum[0] - modelCenter_.X,
                voxelBounds.Maximum[1] - modelCenter_.Y,
                voxelBounds.Maximum[2] - modelCenter_.Z};
            for (std::size_t side = 0U; side < GuideFaces.size(); ++side)
            {
                const auto ghostSide =
                    static_cast<FacePlanGhostSide>(side);
                if ((cell.ExposedFaceMask &
                    FacePlanGhostSideBit(ghostSide)) != 0U)
                    AppendBoxFace(ghostVertices, ghostIndices,
                        minimum, maximum, fillColor,
                        ghostSide);
            }
        }
    }
    else
    {
        const std::size_t ghostBoxCount = ghostPreviewCount *
            (drawIndividualGhostOutlines ? 13U : 1U) +
            (drawIndividualGhostOutlines ||
                ghostPreviewCount == 0U ? 0U : 12U);
        ghostVertices.reserve(ghostBoxCount * 24U);
        ghostIndices.reserve(ghostBoxCount * 36U);
        for (const GhostVoxel& ghost : smartBrushGhostPreview_)
            AppendGhostVoxel(ghostVertices, ghostIndices, ghost, modelCenter_,
                drawIndividualGhostOutlines);
    }
    if (drawIndividualVoxelPreviewOutlines)
    {
        for (const GhostVoxel& ghost : voxelPreviewGhosts_)
            AppendGhostVoxel(ghostVertices, ghostIndices, ghost, modelCenter_,
                true);
    }
    else if (!voxelPreviewGhosts_.empty())
    {
        // Large reusable creations must read as one complete object. Drawing
        // all six faces of every translucent cell creates thousands of
        // overlapping internal surfaces, which look like sliced or missing
        // sides. Materialize only the exact exposed envelope instead.
        const FacePlanGhostSurface stampSurface =
            BuildFacePlanGhostSurface(voxelPreviewGhosts_);
        ghostVertices.reserve(
            ghostVertices.size() + stampSurface.ExposedFaceCount * 4U);
        ghostIndices.reserve(
            ghostIndices.size() + stampSurface.ExposedFaceCount * 6U);
        for (const FacePlanGhostSurfaceCell& cell : stampSurface.Cells)
        {
            const GhostVoxel& ghost =
                voxelPreviewGhosts_[cell.GhostIndex];
            const FacePlanGhostVoxelBounds voxelBounds =
                MakeFacePlanGhostVoxelBounds(ghost.Position);
            std::array<float, 4> fillColor = ghost.Color;
            fillColor[3] = std::clamp(ghost.Alpha, 0.0F, 1.0F);
            const std::array<float, 3> minimum{
                voxelBounds.Minimum[0] - modelCenter_.X,
                voxelBounds.Minimum[1] - modelCenter_.Y,
                voxelBounds.Minimum[2] - modelCenter_.Z};
            const std::array<float, 3> maximum{
                voxelBounds.Maximum[0] - modelCenter_.X,
                voxelBounds.Maximum[1] - modelCenter_.Y,
                voxelBounds.Maximum[2] - modelCenter_.Z};
            for (std::size_t side = 0U; side < GuideFaces.size(); ++side)
            {
                const auto ghostSide =
                    static_cast<FacePlanGhostSide>(side);
                if ((cell.ExposedFaceMask &
                    FacePlanGhostSideBit(ghostSide)) != 0U)
                {
                    AppendBoxFace(ghostVertices, ghostIndices,
                        minimum, maximum, fillColor, ghostSide);
                }
            }
        }
    }
    if (smartBrushGhostGeometryStyle_ ==
            SmartBrushGhostGeometryStyle::VoxelBoxes &&
        !drawIndividualGhostOutlines && ghostPreviewCount != 0U)
    {
        const GhostVoxel* first = !smartBrushGhostPreview_.empty()
            ? &smartBrushGhostPreview_.front()
            : &voxelPreviewGhosts_.front();
        Asset::Voxel::VoxelPosition minimum = first->Position;
        Asset::Voxel::VoxelPosition maximum = minimum;
        const auto extendBounds = [&minimum, &maximum](const GhostVoxel& ghost)
        {
            minimum.X = std::min(minimum.X, ghost.Position.X);
            minimum.Y = std::min(minimum.Y, ghost.Position.Y);
            minimum.Z = std::min(minimum.Z, ghost.Position.Z);
            maximum.X = std::max(maximum.X, ghost.Position.X);
            maximum.Y = std::max(maximum.Y, ghost.Position.Y);
            maximum.Z = std::max(maximum.Z, ghost.Position.Z);
        };
        for (const GhostVoxel& ghost : smartBrushGhostPreview_) extendBounds(ghost);
        for (const GhostVoxel& ghost : voxelPreviewGhosts_) extendBounds(ghost);
        AppendVoxelBoxOutline(ghostVertices, ghostIndices, {minimum, maximum},
            modelCenter_, {0.92F, 0.96F, 1.0F, 0.72F}, 0.024F, 0.022F);
    }
    else if (smartBrushGhostGeometryStyle_ ==
            SmartBrushGhostGeometryStyle::ExposedFaceSurface &&
        !drawIndividualVoxelPreviewOutlines && !voxelPreviewGhosts_.empty())
    {
        Asset::Voxel::VoxelPosition minimum =
            voxelPreviewGhosts_.front().Position;
        Asset::Voxel::VoxelPosition maximum = minimum;
        for (const GhostVoxel& ghost : voxelPreviewGhosts_)
        {
            minimum.X = std::min(minimum.X, ghost.Position.X);
            minimum.Y = std::min(minimum.Y, ghost.Position.Y);
            minimum.Z = std::min(minimum.Z, ghost.Position.Z);
            maximum.X = std::max(maximum.X, ghost.Position.X);
            maximum.Y = std::max(maximum.Y, ghost.Position.Y);
            maximum.Z = std::max(maximum.Z, ghost.Position.Z);
        }
        AppendVoxelBoxOutline(ghostVertices, ghostIndices, {minimum, maximum},
            modelCenter_, {0.92F, 0.96F, 1.0F, 0.72F}, 0.024F, 0.022F);
    }
    if (ghostIndices.empty())
    {
        if (device_ != nullptr)
        {
            if (smartBrushGhostVertexBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device_, smartBrushGhostVertexBuffer_);
            if (smartBrushGhostIndexBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device_, smartBrushGhostIndexBuffer_);
        }
        smartBrushGhostVertexBuffer_ = nullptr;
        smartBrushGhostIndexBuffer_ = nullptr;
        smartBrushGhostIndexCount_ = 0U;
    }
    else if (!UploadBufferPair(
            ghostVertices.data(), ghostVertices.size() * sizeof(GPUVertex),
            ghostIndices.data(), ghostIndices.size() * sizeof(std::uint32_t),
            smartBrushGhostVertexBuffer_, smartBrushGhostIndexBuffer_,
            "Smart Brush Ghost Preview"))
    {
        return false;
    }
    else
    {
        smartBrushGhostIndexCount_ =
            static_cast<std::uint32_t>(ghostIndices.size());
    }

    const auto releaseGizmoGeometry = [this]()
    {
        if (device_ != nullptr)
        {
            if (transformGizmoVisibleVertexBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(
                    device_, transformGizmoVisibleVertexBuffer_);
            if (transformGizmoVisibleIndexBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(
                    device_, transformGizmoVisibleIndexBuffer_);
            if (transformGizmoOccludedVertexBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(
                    device_, transformGizmoOccludedVertexBuffer_);
            if (transformGizmoOccludedIndexBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(
                    device_, transformGizmoOccludedIndexBuffer_);
        }
        transformGizmoVisibleVertexBuffer_ = nullptr;
        transformGizmoVisibleIndexBuffer_ = nullptr;
        transformGizmoOccludedVertexBuffer_ = nullptr;
        transformGizmoOccludedIndexBuffer_ = nullptr;
        transformGizmoVisibleIndexCount_ = 0U;
        transformGizmoOccludedIndexCount_ = 0U;
    };
    if (transformGizmo_ &&
        transformGizmo_->Mode == TransformGizmoMode::Scale &&
        !TransformGizmoRenderPolicy::CenterScreenOverlayEnabled)
    {
        std::vector<GPUVertex> visibleVertices;
        std::vector<std::uint32_t> visibleIndices;
        std::vector<GPUVertex> occludedVertices;
        std::vector<std::uint32_t> occludedIndices;
        visibleVertices.reserve(192U);
        visibleIndices.reserve(288U);
        occludedVertices.reserve(192U);
        occludedIndices.reserve(288U);
        AppendTransformGizmo(
            visibleVertices, visibleIndices, *transformGizmo_);
        AppendTransformGizmo(
            occludedVertices, occludedIndices, *transformGizmo_,
            TransformGizmoRenderPolicy::OccludedColorScale,
            TransformGizmoRenderPolicy::OccludedAlpha,
            TransformGizmoRenderPolicy::OccludedThicknessScale);
        if (visibleIndices.empty() || occludedIndices.empty() ||
            !UploadBufferPair(
                visibleVertices.data(),
                visibleVertices.size() * sizeof(GPUVertex),
                visibleIndices.data(),
                visibleIndices.size() * sizeof(std::uint32_t),
                transformGizmoVisibleVertexBuffer_,
                transformGizmoVisibleIndexBuffer_,
                "visible transform gizmo") ||
            !UploadBufferPair(
                occludedVertices.data(),
                occludedVertices.size() * sizeof(GPUVertex),
                occludedIndices.data(),
                occludedIndices.size() * sizeof(std::uint32_t),
                transformGizmoOccludedVertexBuffer_,
                transformGizmoOccludedIndexBuffer_,
                "occluded transform gizmo"))
        {
            releaseGizmoGeometry();
            return false;
        }
        transformGizmoVisibleIndexCount_ =
            static_cast<std::uint32_t>(visibleIndices.size());
        transformGizmoOccludedIndexCount_ =
            static_cast<std::uint32_t>(occludedIndices.size());
    }
    else
    {
        releaseGizmoGeometry();
    }
    highlightsDirty_ = false;
    ++highlightUploadCount_;
    return true;
}

bool ViewportRenderer::EnsureGuides()
{
    if (!guidesDirty_ && guideVertexBuffer_ != nullptr &&
        guideIndexBuffer_ != nullptr)
    {
        return true;
    }

    const float modelSpan = std::max({guideWidth_, guideHeight_, guideDepth_, 1.0F});
    const float horizontalSpan = std::max({guideWidth_, guideDepth_, 1.0F});
    const float desiredHalfExtent = std::max(4.0F, horizontalSpan * 0.75F);
    const float gridStep = std::max(1.0F, std::ceil(desiredHalfExtent / 20.0F));
    const int gridHalfCount = static_cast<int>(
        std::ceil(desiredHalfExtent / gridStep));
    const float gridExtent = gridHalfCount * gridStep;
    const float lineWidth = std::max(0.018F, gridStep * 0.025F);
    const float lineHeight = std::max(0.008F, gridStep * 0.008F);
    const float groundY = -(std::max(guideHeight_, 1.0F) * 0.5F) - lineHeight;

    std::vector<GPUVertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(static_cast<std::size_t>(gridHalfCount * 4 + 5) * 24U);
    indices.reserve(static_cast<std::size_t>(gridHalfCount * 4 + 5) * 36U);
    for (int line = -gridHalfCount; line <= gridHalfCount; ++line)
    {
        const float offset = static_cast<float>(line) * gridStep;
        const bool major = line == 0 || (line % 5) == 0;
        const std::array<float, 4> color = major
            ? std::array<float, 4>{0.34F, 0.36F, 0.40F, 1.0F}
            : std::array<float, 4>{0.20F, 0.22F, 0.25F, 1.0F};
        const float halfWidth = major ? lineWidth : lineWidth * 0.55F;
        AppendBox(vertices, indices,
            {-gridExtent, groundY - lineHeight, offset - halfWidth},
            {gridExtent, groundY, offset + halfWidth}, color);
        AppendBox(vertices, indices,
            {offset - halfWidth, groundY - lineHeight, -gridExtent},
            {offset + halfWidth, groundY, gridExtent}, color);
    }
    if (guideWidth_ > 0.0F && guideHeight_ > 0.0F && guideDepth_ > 0.0F)
    {
        const float minX = -guideWidth_ * 0.5F;
        const float maxX = guideWidth_ * 0.5F;
        const float minY = -guideHeight_ * 0.5F;
        const float maxY = guideHeight_ * 0.5F;
        const float minZ = -guideDepth_ * 0.5F;
        const float maxZ = guideDepth_ * 0.5F;
        const float edge = std::max(0.025F, modelSpan * 0.0025F);
        constexpr std::array<float, 4> boundsColor{
            0.30F, 0.62F, 0.92F, 0.72F};
        for (const float y : {minY, maxY})
            for (const float z : {minZ, maxZ})
                AppendBox(vertices, indices,
                    {minX, y - edge, z - edge},
                    {maxX, y + edge, z + edge}, boundsColor);
        for (const float x : {minX, maxX})
            for (const float z : {minZ, maxZ})
                AppendBox(vertices, indices,
                    {x - edge, minY, z - edge},
                    {x + edge, maxY, z + edge}, boundsColor);
        for (const float x : {minX, maxX})
            for (const float y : {minY, maxY})
                AppendBox(vertices, indices,
                    {x - edge, y - edge, minZ},
                    {x + edge, y + edge, maxZ}, boundsColor);
    }
    gridIndexCount_ = static_cast<std::uint32_t>(indices.size());

    const float axisLength = std::max(2.0F, modelSpan * 0.65F);
    const float axisHalfWidth = std::max(0.025F, modelSpan * 0.006F);
    AppendBox(vertices, indices,
        {0.0F, -axisHalfWidth, -axisHalfWidth},
        {axisLength, axisHalfWidth, axisHalfWidth},
        {0.95F, 0.12F, 0.10F, 1.0F});
    AppendBox(vertices, indices,
        {-axisHalfWidth, 0.0F, -axisHalfWidth},
        {axisHalfWidth, axisLength, axisHalfWidth},
        {0.12F, 0.90F, 0.22F, 1.0F});
    AppendBox(vertices, indices,
        {-axisHalfWidth, -axisHalfWidth, 0.0F},
        {axisHalfWidth, axisHalfWidth, axisLength},
        {0.12F, 0.32F, 0.98F, 1.0F});
    axesIndexCount_ = static_cast<std::uint32_t>(indices.size()) - gridIndexCount_;

    const std::size_t vertexBytes = vertices.size() * sizeof(GPUVertex);
    const std::size_t indexBytes = indices.size() * sizeof(std::uint32_t);
    if (!UploadBufferPair(
            vertices.data(), vertexBytes, indices.data(), indexBytes,
            guideVertexBuffer_, guideIndexBuffer_, "viewport guides"))
    {
        return false;
    }
    guidesDirty_ = false;
    return true;
}

bool ViewportRenderer::EnsureTargets(
    const std::uint32_t width,
    const std::uint32_t height)
{
    if (colorTarget_ != nullptr && width_ == width && height_ == height)
    {
        return true;
    }
    SDL_WaitForGPUIdle(device_);
    ReleaseTargets();
    const SDL_GPUTextureCreateInfo colorInfo{
        SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
        width, height, 1U, 1U, SDL_GPU_SAMPLECOUNT_1, 0U};
    const SDL_PropertiesID depthProperties = SDL_CreateProperties();
    if (depthProperties == 0U ||
        !SDL_SetFloatProperty(
            depthProperties,
            SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_DEPTH_FLOAT,
            ViewportDepthClearValue) ||
        !SDL_SetNumberProperty(
            depthProperties,
            SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_STENCIL_NUMBER,
            ViewportStencilClearValue))
    {
        if (depthProperties != 0U)
        {
            SDL_DestroyProperties(depthProperties);
        }
        SetError(
            std::string("Unable to configure viewport depth target: ") +
            SDL_GetError());
        return false;
    }
    const SDL_GPUTextureCreateInfo depthInfo{
        SDL_GPU_TEXTURETYPE_2D, ToSDLDepthFormat(depthFormat_),
        SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        width, height, 1U, 1U, SDL_GPU_SAMPLECOUNT_1, depthProperties};
    colorTarget_ = SDL_CreateGPUTexture(device_, &colorInfo);
    depthTarget_ = SDL_CreateGPUTexture(device_, &depthInfo);
    SDL_DestroyProperties(depthProperties);
    if (colorTarget_ == nullptr || depthTarget_ == nullptr)
    {
        ReleaseTargets();
        SetError(std::string("Unable to create viewport targets: ") + SDL_GetError());
        return false;
    }
    width_ = width;
    height_ = height;
    return true;
}

bool ViewportRenderer::Render(
    const std::uint32_t width,
    const std::uint32_t height,
    const EditorCamera& camera,
    const bool showGrid,
    const bool showAxes,
    const std::array<float, 4>& backgroundColor)
{
    if (width == 0U || height == 0U || !EnsurePipeline() ||
        !EnsureTargets(width, height) ||
        ((showGrid || showAxes) && !EnsureGuides()) ||
        (highlightsDirty_ && !EnsureHighlights()))
    {
        return false;
    }
    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device_);
    if (commandBuffer == nullptr)
    {
        SetError(std::string("Unable to acquire viewport command buffer: ") + SDL_GetError());
        return false;
    }
    const std::array<float, 16> viewProjection = camera.GetViewProjection();
    SDL_PushGPUVertexUniformData(
        commandBuffer, 0U, viewProjection.data(), sizeof(viewProjection));
    SDL_GPUColorTargetInfo colorInfo{};
    colorInfo.texture = colorTarget_;
    colorInfo.clear_color = {
        backgroundColor[0], backgroundColor[1],
        backgroundColor[2], backgroundColor[3]};
    colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    colorInfo.store_op = SDL_GPU_STOREOP_STORE;
    colorInfo.cycle = true;
    SDL_GPUDepthStencilTargetInfo depthInfo{};
    depthInfo.texture = depthTarget_;
    depthInfo.clear_depth = ViewportDepthClearValue;
    depthInfo.clear_stencil =
        static_cast<std::uint8_t>(ViewportStencilClearValue);
    depthInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    depthInfo.store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthInfo.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
    depthInfo.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
    depthInfo.cycle = true;
    SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(
        commandBuffer, &colorInfo, 1U, &depthInfo);
    if (pass == nullptr)
    {
        const std::string error = SDL_GetError();
        SDL_CancelGPUCommandBuffer(commandBuffer);
        SetError("Unable to begin viewport render pass: " + error);
        return false;
    }
    const SDL_GPUViewport viewport{
        0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height),
        0.0F, 1.0F};
    const SDL_Rect scissor{
        0, 0, static_cast<int>(width), static_cast<int>(height)};
    SDL_SetGPUViewport(pass, &viewport);
    SDL_SetGPUScissor(pass, &scissor);
    SDL_BindGPUGraphicsPipeline(pass, pipeline_);
    if (showGrid || showAxes)
    {
        const SDL_GPUBufferBinding guideVertexBinding{guideVertexBuffer_, 0U};
        const SDL_GPUBufferBinding guideIndexBinding{guideIndexBuffer_, 0U};
        SDL_BindGPUVertexBuffers(pass, 0U, &guideVertexBinding, 1U);
        SDL_BindGPUIndexBuffer(
            pass, &guideIndexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        if (showGrid)
        {
            SDL_DrawGPUIndexedPrimitives(
                pass, gridIndexCount_, 1U, 0U, 0, 0U);
        }
        if (showAxes)
        {
            SDL_DrawGPUIndexedPrimitives(
                pass, axesIndexCount_, 1U, gridIndexCount_, 0, 0U);
        }
    }
    if (exactPreviewChunksActive_ && !modelChunks_.empty())
    {
        // VF-0265 (lot 3f) : surimpression. On dessine les chunks du modèle en
        // substituant ceux que la preview remplace ; les buffers du modèle ne
        // sont ni relâchés ni réécrits, donc sortir de la preview est gratuit.
        bool drewChunk = false;
        const auto drawChunk = [&](const ModelChunkBuffers& chunk)
        {
            if (chunk.IndexCount == 0U || chunk.VertexBuffer == nullptr ||
                chunk.IndexBuffer == nullptr)
                return;
            const SDL_GPUBufferBinding vertexBinding{chunk.VertexBuffer, 0U};
            const SDL_GPUBufferBinding indexBinding{chunk.IndexBuffer, 0U};
            SDL_BindGPUVertexBuffers(pass, 0U, &vertexBinding, 1U);
            SDL_BindGPUIndexBuffer(
                pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_DrawGPUIndexedPrimitives(
                pass, chunk.IndexCount, 1U, 0U, 0, 0U);
            drewChunk = true;
        };
        for (const auto& entry : modelChunks_)
        {
            const auto override = exactPreviewChunks_.find(entry.first);
            // Un override présent mais vide masque volontairement ce chunk.
            drawChunk(override != exactPreviewChunks_.end()
                    ? override->second
                    : entry.second);
        }
        for (const auto& entry : exactPreviewChunks_)
        {
            // Chunks que la preview crée là où le modèle n'en a pas.
            if (modelChunks_.find(entry.first) == modelChunks_.end())
                drawChunk(entry.second);
        }
        if (drewChunk) ++modelRenderCount_;
    }
    else if (exactPreviewActive_ || indexCount_ > 0U)
    {
        SDL_GPUBuffer* const visibleVertexBuffer = exactPreviewActive_
            ? exactPreviewVertexBuffer_ : vertexBuffer_;
        SDL_GPUBuffer* const visibleIndexBuffer = exactPreviewActive_
            ? exactPreviewIndexBuffer_ : indexBuffer_;
        const std::uint32_t visibleIndexCount = exactPreviewActive_
            ? exactPreviewIndexCount_ : indexCount_;
        if (visibleIndexCount > 0U)
        {
            const SDL_GPUBufferBinding vertexBinding{visibleVertexBuffer, 0U};
            const SDL_GPUBufferBinding indexBinding{visibleIndexBuffer, 0U};
            SDL_BindGPUVertexBuffers(pass, 0U, &vertexBinding, 1U);
            SDL_BindGPUIndexBuffer(
                pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_DrawGPUIndexedPrimitives(
                pass, visibleIndexCount, 1U, 0U, 0, 0U);
            ++modelRenderCount_;
        }
    }
    else if (!modelChunks_.empty())
    {
        // VF-0262 (lot 262-4): chunked model — one draw per chunk, same
        // pipeline and uniforms.
        bool drewModelChunk = false;
        for (const auto& entry : modelChunks_)
        {
            const ModelChunkBuffers& chunk = entry.second;
            if (chunk.IndexCount == 0U || chunk.VertexBuffer == nullptr ||
                chunk.IndexBuffer == nullptr)
                continue;
            const SDL_GPUBufferBinding vertexBinding{chunk.VertexBuffer, 0U};
            const SDL_GPUBufferBinding indexBinding{chunk.IndexBuffer, 0U};
            SDL_BindGPUVertexBuffers(pass, 0U, &vertexBinding, 1U);
            SDL_BindGPUIndexBuffer(
                pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
            SDL_DrawGPUIndexedPrimitives(
                pass, chunk.IndexCount, 1U, 0U, 0, 0U);
            drewModelChunk = true;
        }
        if (drewModelChunk) ++modelRenderCount_;
    }
    if (interactionV2MoveActive_ &&
        interactionV2MoveIndexCount_ > 0U &&
        interactionV2MoveVertexBuffer_ != nullptr &&
        interactionV2MoveIndexBuffer_ != nullptr)
    {
        const Vec3 translation{
            static_cast<float>(interactionV2MoveDelta_.X),
            static_cast<float>(interactionV2MoveDelta_.Y),
            static_cast<float>(interactionV2MoveDelta_.Z)};
        const Matrix4 translatedViewProjection = MultiplyMatrix(
            viewProjection, TranslationMatrix(translation));
        SDL_PushGPUVertexUniformData(
            commandBuffer, 0U, translatedViewProjection.data(),
            sizeof(translatedViewProjection));
        const SDL_GPUBufferBinding vertexBinding{
            interactionV2MoveVertexBuffer_, 0U};
        const SDL_GPUBufferBinding indexBinding{
            interactionV2MoveIndexBuffer_, 0U};
        SDL_BindGPUVertexBuffers(pass, 0U, &vertexBinding, 1U);
        SDL_BindGPUIndexBuffer(
            pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(
            pass, interactionV2MoveIndexCount_, 1U, 0U, 0, 0U);
        ++interactionV2MoveDrawCount_;
        // Every following presentation element remains in document space.
        SDL_PushGPUVertexUniformData(
            commandBuffer, 0U, viewProjection.data(), sizeof(viewProjection));
    }
    if (smartBrushGhostIndexCount_ > 0U)
    {
        SDL_BindGPUGraphicsPipeline(pass, smartBrushGhostPipeline_);
        const SDL_GPUBufferBinding vertexBinding{
            smartBrushGhostVertexBuffer_, 0U};
        const SDL_GPUBufferBinding indexBinding{
            smartBrushGhostIndexBuffer_, 0U};
        SDL_BindGPUVertexBuffers(pass, 0U, &vertexBinding, 1U);
        SDL_BindGPUIndexBuffer(
            pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(
            pass, smartBrushGhostIndexCount_, 1U, 0U, 0, 0U);
        // The conventional highlights remain opaque and use the unchanged
        // model pipeline.
        SDL_BindGPUGraphicsPipeline(pass, pipeline_);
    }
    if (highlightIndexCount_ > 0U)
    {
        const SDL_GPUBufferBinding vertexBinding{highlightVertexBuffer_, 0U};
        const SDL_GPUBufferBinding indexBinding{highlightIndexBuffer_, 0U};
        SDL_BindGPUVertexBuffers(pass, 0U, &vertexBinding, 1U);
        SDL_BindGPUIndexBuffer(
            pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(
            pass, highlightIndexCount_, 1U, 0U, 0, 0U);
        ++highlightRenderCount_;
    }
    if (transformGizmoOccludedIndexCount_ > 0U)
    {
        SDL_BindGPUGraphicsPipeline(pass, transformGizmoOccludedPipeline_);
        const SDL_GPUBufferBinding vertexBinding{
            transformGizmoOccludedVertexBuffer_, 0U};
        const SDL_GPUBufferBinding indexBinding{
            transformGizmoOccludedIndexBuffer_, 0U};
        SDL_BindGPUVertexBuffers(pass, 0U, &vertexBinding, 1U);
        SDL_BindGPUIndexBuffer(
            pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(
            pass, transformGizmoOccludedIndexCount_, 1U, 0U, 0, 0U);
    }
    if (transformGizmoVisibleIndexCount_ > 0U)
    {
        SDL_BindGPUGraphicsPipeline(pass, transformGizmoVisiblePipeline_);
        const SDL_GPUBufferBinding vertexBinding{
            transformGizmoVisibleVertexBuffer_, 0U};
        const SDL_GPUBufferBinding indexBinding{
            transformGizmoVisibleIndexBuffer_, 0U};
        SDL_BindGPUVertexBuffers(pass, 0U, &vertexBinding, 1U);
        SDL_BindGPUIndexBuffer(
            pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(
            pass, transformGizmoVisibleIndexCount_, 1U, 0U, 0, 0U);
    }
    SDL_EndGPURenderPass(pass);
    if (!SDL_SubmitGPUCommandBuffer(commandBuffer))
    {
        SetError(std::string("Unable to submit viewport rendering: ") + SDL_GetError());
        return false;
    }
    lastError_.clear();
    return true;
}

void ViewportRenderer::ClearModel() noexcept
{
    ReleaseWholeModelBuffers();
    // VF-0265 (lot 3f) : les overrides décrivent les chunks de CE modèle. Sans
    // modèle, ils n'ont plus de sens et laisseraient des buffers orphelins.
    ReleaseExactPreviewChunks();
    ReleaseModelChunks();
    ClearExactPreviewMesh();
    ConfigureVoxelPreview(nullptr);
    ConfigureTransformPreview(nullptr);
    ConfigureTransformGizmo(nullptr);
    ConfigureHighlights(
        std::nullopt, std::span<const Asset::Voxel::VoxelPosition>{},
        std::nullopt, std::nullopt,
        false, SelectionBoxVisualState::Normal,
        std::nullopt, VoxelPlacementPreviewStyle::PencilInvalid,
        std::span<const Asset::Voxel::VoxelPosition>{},
        std::span<const Asset::Voxel::VoxelPosition>{}, std::nullopt,
        std::nullopt,
        std::nullopt, std::span<const Asset::Voxel::VoxelPosition>{},
        std::nullopt, nullptr,
        SmartBrushGhostGeometryStyle::VoxelBoxes, {});
    highlightGeometry_.reset();
    smartBrushGhostGeometry_.reset();
}

void ViewportRenderer::ClearExactPreviewMesh() noexcept
{
    if (device_ != nullptr)
    {
        if (exactPreviewVertexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, exactPreviewVertexBuffer_);
        if (exactPreviewIndexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, exactPreviewIndexBuffer_);
    }
    exactPreviewVertexBuffer_ = nullptr;
    exactPreviewIndexBuffer_ = nullptr;
    exactPreviewIndexCount_ = 0U;
    exactPreviewActive_ = false;
    exactPreviewDocumentIdentity_ = 0U;
    exactPreviewDocumentRevision_ = 0U;
    exactPreviewPlanId_ = 0U;
    exactPreviewPlanRevision_ = 0U;
    // VF-0265 (lot 3f) : « plus de preview » doit vouloir dire plus de preview,
    // quel que soit le chemin utilisé. Sans cela, désactiver la preview
    // monolithique laisserait les overrides chunkés à l'écran.
    ReleaseExactPreviewChunks();
}

void ViewportRenderer::ReleaseInteractionV2MoveSource() noexcept
{
    if (device_ != nullptr)
    {
        if (interactionV2MoveVertexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, interactionV2MoveVertexBuffer_);
        if (interactionV2MoveIndexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, interactionV2MoveIndexBuffer_);
    }
    interactionV2MoveVertexBuffer_ = nullptr;
    interactionV2MoveIndexBuffer_ = nullptr;
    interactionV2MoveIndexCount_ = 0U;
    interactionV2MoveSourceDirty_ = false;
}

void ViewportRenderer::ReleaseHighlights() noexcept
{
    ReleaseInteractionV2MoveSource();
    if (device_ != nullptr)
    {
        if (highlightVertexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, highlightVertexBuffer_);
        if (highlightIndexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, highlightIndexBuffer_);
        if (interactionV2HighlightTransferBuffer_ != nullptr)
            SDL_ReleaseGPUTransferBuffer(
                device_, interactionV2HighlightTransferBuffer_);
        if (smartBrushGhostVertexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, smartBrushGhostVertexBuffer_);
        if (smartBrushGhostIndexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, smartBrushGhostIndexBuffer_);
        if (transformGizmoVisibleVertexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(
                device_, transformGizmoVisibleVertexBuffer_);
        if (transformGizmoVisibleIndexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(
                device_, transformGizmoVisibleIndexBuffer_);
        if (transformGizmoOccludedVertexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(
                device_, transformGizmoOccludedVertexBuffer_);
        if (transformGizmoOccludedIndexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(
                device_, transformGizmoOccludedIndexBuffer_);
    }
    highlightVertexBuffer_ = nullptr;
    highlightIndexBuffer_ = nullptr;
    interactionV2HighlightTransferBuffer_ = nullptr;
    smartBrushGhostVertexBuffer_ = nullptr;
    smartBrushGhostIndexBuffer_ = nullptr;
    highlightIndexCount_ = 0U;
    interactionV2HighlightVertexCapacity_ = 0U;
    interactionV2HighlightIndexCapacity_ = 0U;
    interactionV2HighlightTransferCapacity_ = 0U;
    smartBrushGhostIndexCount_ = 0U;
    transformGizmoVisibleVertexBuffer_ = nullptr;
    transformGizmoVisibleIndexBuffer_ = nullptr;
    transformGizmoOccludedVertexBuffer_ = nullptr;
    transformGizmoOccludedIndexBuffer_ = nullptr;
    transformGizmoVisibleIndexCount_ = 0U;
    transformGizmoOccludedIndexCount_ = 0U;
}

void ViewportRenderer::ReleaseGuides() noexcept
{
    if (device_ != nullptr)
    {
        if (guideVertexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, guideVertexBuffer_);
        if (guideIndexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, guideIndexBuffer_);
    }
    guideVertexBuffer_ = nullptr;
    guideIndexBuffer_ = nullptr;
    gridIndexCount_ = 0U;
    axesIndexCount_ = 0U;
    guidesDirty_ = true;
}

void ViewportRenderer::ReleaseTargets() noexcept
{
    if (device_ != nullptr)
    {
        if (colorTarget_ != nullptr) SDL_ReleaseGPUTexture(device_, colorTarget_);
        if (depthTarget_ != nullptr) SDL_ReleaseGPUTexture(device_, depthTarget_);
    }
    colorTarget_ = nullptr;
    depthTarget_ = nullptr;
    width_ = 0U;
    height_ = 0U;
}

void ViewportRenderer::Shutdown() noexcept
{
    if (device_ != nullptr) SDL_WaitForGPUIdle(device_);
    ClearModel();
    ReleaseGuides();
    ReleaseHighlights();
    ReleaseTargets();
    if (device_ != nullptr && pipeline_ != nullptr)
    {
        SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
    }
    if (device_ != nullptr && smartBrushGhostPipeline_ != nullptr)
        SDL_ReleaseGPUGraphicsPipeline(device_, smartBrushGhostPipeline_);
    if (device_ != nullptr && transformGizmoVisiblePipeline_ != nullptr)
        SDL_ReleaseGPUGraphicsPipeline(
            device_, transformGizmoVisiblePipeline_);
    if (device_ != nullptr && transformGizmoOccludedPipeline_ != nullptr)
        SDL_ReleaseGPUGraphicsPipeline(
            device_, transformGizmoOccludedPipeline_);
    pipeline_ = nullptr;
    smartBrushGhostPipeline_ = nullptr;
    transformGizmoVisiblePipeline_ = nullptr;
    transformGizmoOccludedPipeline_ = nullptr;
    depthFormat_ = ViewportDepthFormat::Unavailable;
    device_ = nullptr;
}

SDL_GPUTexture* ViewportRenderer::Texture() const noexcept
{
    return colorTarget_;
}

const std::string& ViewportRenderer::LastError() const noexcept
{
    return lastError_;
}

std::size_t ViewportRenderer::HighlightUploadCount() const noexcept
{
    return highlightUploadCount_;
}

std::size_t ViewportRenderer::InteractionV2UploadCount() const noexcept
{
    return interactionV2UploadCount_;
}

std::size_t
ViewportRenderer::InteractionV2BufferRecreationCount() const noexcept
{
    return interactionV2BufferRecreationCount_;
}

std::size_t ViewportRenderer::InteractionV2UploadedBytes() const noexcept
{
    return interactionV2UploadedBytes_;
}

std::size_t
ViewportRenderer::InteractionV2MoveSourceUploadCount() const noexcept
{
    return interactionV2MoveSourceUploadCount_;
}

std::size_t
ViewportRenderer::InteractionV2MoveSourceUploadedBytes() const noexcept
{
    return interactionV2MoveSourceUploadedBytes_;
}

std::size_t
ViewportRenderer::InteractionV2MoveDeltaUpdateCount() const noexcept
{
    return interactionV2MoveDeltaUpdateCount_;
}

std::size_t ViewportRenderer::InteractionV2MoveDrawCount() const noexcept
{
    return interactionV2MoveDrawCount_;
}

std::size_t ViewportRenderer::HighlightRenderCount() const noexcept
{
    return highlightRenderCount_;
}

std::size_t ViewportRenderer::ModelRenderCount() const noexcept
{
    return modelRenderCount_;
}

std::size_t ViewportRenderer::ModelUploadCount() const noexcept
{
    return modelUploadCount_;
}

bool ViewportRenderer::HasModelMesh() const noexcept
{
    return (vertexBuffer_ != nullptr && indexBuffer_ != nullptr &&
        indexCount_ > 0U) || !modelChunks_.empty();
}

std::size_t ViewportRenderer::ModelChunkCount() const noexcept
{
    return modelChunks_.size();
}

bool ViewportRenderer::HasExactPreviewMesh() const noexcept
{
    return exactPreviewActive_;
}

bool ViewportRenderer::HasHighlightMesh() const noexcept
{
    return highlightVertexBuffer_ != nullptr &&
        highlightIndexBuffer_ != nullptr && highlightIndexCount_ > 0U;
}

bool ViewportRenderer::HasTransformPreview() const noexcept
{
    return transformPreview_ != nullptr;
}

std::size_t ViewportRenderer::TransformPreviewSourcePrimitiveCount()
    const noexcept
{
    if (!transformPreview_) return 0U;
    if (!transformPreview_->DrawSourceGhost) return 0U;
    return transformPreview_->Plan.DrawIndividualVoxels
        ? transformPreview_->SourcePositions.size()
        : static_cast<std::size_t>(transformPreview_->SourceBounds.Valid);
}

std::size_t ViewportRenderer::TransformPreviewDestinationPrimitiveCount()
    const noexcept
{
    if (!transformPreview_) return 0U;
    return transformPreview_->Plan.DrawIndividualVoxels
        ? transformPreview_->Placement.Instances().size()
        : static_cast<std::size_t>(transformPreview_->PreviewBounds.Valid);
}

std::size_t ViewportRenderer::TransformPreviewCollisionPrimitiveCount()
    const noexcept
{
    if (!transformPreview_) return 0U;
    if (transformPreview_->Plan.DrawIndividualVoxels ||
        transformPreview_->Plan.DrawIndividualCollisions)
        return transformPreview_->Plan.CollisionVoxelCount +
            transformPreview_->Plan.OutOfBoundsVoxelCount;
    return static_cast<std::size_t>(
               transformPreview_->CollisionBounds.Valid) +
        static_cast<std::size_t>(
               transformPreview_->OutOfBoundsBounds.Valid);
}

bool ViewportRenderer::HasTransformGizmo() const noexcept
{
    return transformGizmo_.has_value();
}

std::size_t ViewportRenderer::TransformGizmoAxisPrimitiveCount() const noexcept
{
    return transformGizmo_
        ? TransformGizmoModel::AxisPrimitiveCount : 0U;
}

void ViewportRenderer::SetError(std::string message)
{
    lastError_ = std::move(message);
}

} // namespace VoxelForge::Editor
