#include "ViewportRenderer.h"
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

void AppendBox(
    std::vector<GPUVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const std::array<float, 3>& minimum,
    const std::array<float, 3>& maximum,
    const std::array<float, 4>& color)
{
    for (const GuideFace& face : GuideFaces)
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
    }
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
    std::vector<TransformPreviewVoxel> Voxels;
    std::vector<Asset::Voxel::VoxelPosition> SourcePositions;
    std::array<Asset::Voxel::VoxelColor, 256U> Palette{};
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
    if (pipeline_ != nullptr && transformGizmoVisiblePipeline_ != nullptr &&
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
    pipelineInfo.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    pipelineInfo.target_info.has_depth_stencil_target = true;

    pipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipelineInfo.depth_stencil_state.enable_depth_test = true;
    pipelineInfo.depth_stencil_state.enable_depth_write = true;
    pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);

    pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipelineInfo.depth_stencil_state.enable_depth_test =
        TransformGizmoRenderPolicy::VisiblePassDepthTestEnabled;
    pipelineInfo.depth_stencil_state.enable_depth_write =
        TransformGizmoRenderPolicy::VisiblePassDepthWriteEnabled;
    transformGizmoVisiblePipeline_ =
        SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);

    colorDescription.blend_state.src_color_blendfactor =
        SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    colorDescription.blend_state.dst_color_blendfactor =
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorDescription.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colorDescription.blend_state.src_alpha_blendfactor =
        SDL_GPU_BLENDFACTOR_ONE;
    colorDescription.blend_state.dst_alpha_blendfactor =
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colorDescription.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
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
    if (pipeline_ == nullptr || transformGizmoVisiblePipeline_ == nullptr ||
        transformGizmoOccludedPipeline_ == nullptr)
    {
        if (pipeline_ != nullptr)
            SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
        if (transformGizmoVisiblePipeline_ != nullptr)
            SDL_ReleaseGPUGraphicsPipeline(
                device_, transformGizmoVisiblePipeline_);
        if (transformGizmoOccludedPipeline_ != nullptr)
            SDL_ReleaseGPUGraphicsPipeline(
                device_, transformGizmoOccludedPipeline_);
        pipeline_ = nullptr;
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
            vertexBuffer_, indexBuffer_, "voxel model"))
    {
        return false;
    }
    indexCount_ = static_cast<std::uint32_t>(mesh.IndexCount());
    ++modelUploadCount_;
    lastError_.clear();
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
    std::optional<VoxelBoxBounds> boxPreview,
    const std::span<const Asset::Voxel::VoxelPosition> linePreview,
    std::optional<VoxelSpherePreview> spherePreview,
    const Vec3 modelCenter) noexcept
{
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
        boxPreviewHighlight_ == boxPreview &&
        equals(linePreviewHighlights_, linePreview) &&
        spherePreviewHighlight_ == spherePreview &&
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
    boxPreviewHighlight_ = boxPreview;
    linePreviewHighlights_.assign(linePreview.begin(), linePreview.end());
    spherePreviewHighlight_ = spherePreview;
    modelCenter_ = modelCenter;
    highlightsDirty_ = hoveredHighlight_.has_value() ||
        !selectedHighlights_.empty() ||
        selectionBoundsHighlight_.has_value() ||
        editableSelectionBoundsHighlight_.has_value() ||
        placementPreviewHighlight_.has_value() ||
        boxPreviewHighlight_.has_value() || !linePreviewHighlights_.empty() ||
        spherePreviewHighlight_.has_value() || transformPreview_ != nullptr ||
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
        snapshot.Voxels.assign(preview->Voxels.begin(), preview->Voxels.end());
        snapshot.SourcePositions.assign(
            preview->SourcePositions.begin(), preview->SourcePositions.end());
        std::fill(snapshot.Palette.begin(), snapshot.Palette.end(),
            Asset::Voxel::VoxelColor{});
        std::copy_n(preview->Palette.begin(),
            std::min(preview->Palette.size(), snapshot.Palette.size()),
            snapshot.Palette.begin());
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
        spherePreviewHighlight_.has_value() || transformPreview_ != nullptr ||
        transformGizmo_.has_value();
    if (!highlightsDirty_) ReleaseHighlights();
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
        spherePreviewHighlight_.has_value() || transformPreview_ != nullptr ||
        transformGizmo_.has_value();
    if (!highlightsDirty_) ReleaseHighlights();
}

bool ViewportRenderer::EnsureHighlights()
{
    if (!highlightsDirty_) return true;
    if (!highlightGeometry_)
        highlightGeometry_ = std::make_unique<HighlightGeometryCache>();
    std::vector<GPUVertex>& vertices = highlightGeometry_->Vertices;
    std::vector<std::uint32_t>& indices = highlightGeometry_->Indices;
    vertices.clear();
    indices.clear();
    const std::size_t outlineCount =
        static_cast<std::size_t>(placementPreviewHighlight_.has_value()) +
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
                transformPreview_->Voxels.size();
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
        (spherePreviewHighlight_ ? sphereBoxCount : 0U);
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
                : InvalidPlacementPreviewColor);
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
        const bool hovered = selectionBoxVisualState_ ==
            SelectionBoxVisualState::Hovered;
        AppendVoxelBoxOutline(vertices, indices,
            {bounds.Minimum, bounds.Maximum}, modelCenter_,
            moving
                ? std::array<float, 4>{0.22F, 1.0F, 0.84F, 1.0F}
                : hovered
                ? std::array<float, 4>{0.12F, 1.0F, 0.78F, 1.0F}
                : std::array<float, 4>{0.08F, 0.98F, 0.72F, 1.0F},
            moving ? 0.080F : hovered ? 0.072F : 0.065F,
            moving ? 0.085F : hovered ? 0.078F : 0.070F);
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
            for (const TransformPreviewVoxel& voxel : preview.Voxels)
            {
                std::array<float, 4> color{};
                if (voxel.State == TransformPreviewVoxelState::Collision)
                    color = collisionColor;
                else if (voxel.State ==
                    TransformPreviewVoxelState::OutOfBounds)
                    color = outOfBoundsColor;
                else
                {
                    const Asset::Voxel::VoxelColor paletteColor =
                        preview.Palette[voxel.Value.PaletteIndex];
                    constexpr float scale = 1.0F / 255.0F;
                    color = {
                        paletteColor.Red * scale,
                        paletteColor.Green * scale,
                        paletteColor.Blue * scale,
                        1.0F};
                }
                AppendVoxelOutline(vertices, indices, voxel.PreviewPosition,
                    modelCenter_, color);
            }
        }
        else if (preview.Plan.DrawIndividualCollisions)
        {
            for (const TransformPreviewVoxel& voxel : preview.Voxels)
            {
                if (voxel.State == TransformPreviewVoxelState::Collision)
                    AppendVoxelOutline(vertices, indices,
                        voxel.PreviewPosition, modelCenter_, collisionColor);
                else if (voxel.State ==
                    TransformPreviewVoxelState::OutOfBounds)
                    AppendVoxelOutline(vertices, indices,
                        voxel.PreviewPosition, modelCenter_, outOfBoundsColor);
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
        if (device_ != nullptr)
        {
            if (highlightVertexBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device_, highlightVertexBuffer_);
            if (highlightIndexBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device_, highlightIndexBuffer_);
        }
        highlightVertexBuffer_ = nullptr;
        highlightIndexBuffer_ = nullptr;
        highlightIndexCount_ = 0U;
    }
    else if (!UploadBufferPair(
            vertices.data(), vertices.size() * sizeof(GPUVertex),
            indices.data(), indices.size() * sizeof(std::uint32_t),
            highlightVertexBuffer_, highlightIndexBuffer_,
            "voxel selection highlights"))
    {
        return false;
    }
    else
    {
        highlightIndexCount_ = static_cast<std::uint32_t>(indices.size());
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
        transformGizmo_->Mode != TransformGizmoMode::Rotate)
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
        SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREFORMAT_D16_UNORM,
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
    if (indexCount_ > 0U)
    {
        const SDL_GPUBufferBinding vertexBinding{vertexBuffer_, 0U};
        const SDL_GPUBufferBinding indexBinding{indexBuffer_, 0U};
        SDL_BindGPUVertexBuffers(pass, 0U, &vertexBinding, 1U);
        SDL_BindGPUIndexBuffer(
            pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
        SDL_DrawGPUIndexedPrimitives(pass, indexCount_, 1U, 0U, 0, 0U);
        ++modelRenderCount_;
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
    if (device_ != nullptr)
    {
        if (vertexBuffer_ != nullptr) SDL_ReleaseGPUBuffer(device_, vertexBuffer_);
        if (indexBuffer_ != nullptr) SDL_ReleaseGPUBuffer(device_, indexBuffer_);
    }
    vertexBuffer_ = nullptr;
    indexBuffer_ = nullptr;
    indexCount_ = 0U;
    ConfigureTransformPreview(nullptr);
    ConfigureTransformGizmo(nullptr);
    ConfigureHighlights(
        std::nullopt, std::span<const Asset::Voxel::VoxelPosition>{},
        std::nullopt, std::nullopt,
        false, SelectionBoxVisualState::Normal,
        std::nullopt, VoxelPlacementPreviewStyle::PencilInvalid,
        std::nullopt, std::span<const Asset::Voxel::VoxelPosition>{},
        std::nullopt, {});
    highlightGeometry_.reset();
}

void ViewportRenderer::ReleaseHighlights() noexcept
{
    if (device_ != nullptr)
    {
        if (highlightVertexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, highlightVertexBuffer_);
        if (highlightIndexBuffer_ != nullptr)
            SDL_ReleaseGPUBuffer(device_, highlightIndexBuffer_);
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
    highlightIndexCount_ = 0U;
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
    if (device_ != nullptr && transformGizmoVisiblePipeline_ != nullptr)
        SDL_ReleaseGPUGraphicsPipeline(
            device_, transformGizmoVisiblePipeline_);
    if (device_ != nullptr && transformGizmoOccludedPipeline_ != nullptr)
        SDL_ReleaseGPUGraphicsPipeline(
            device_, transformGizmoOccludedPipeline_);
    pipeline_ = nullptr;
    transformGizmoVisiblePipeline_ = nullptr;
    transformGizmoOccludedPipeline_ = nullptr;
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
    return vertexBuffer_ != nullptr && indexBuffer_ != nullptr &&
        indexCount_ > 0U;
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
        ? transformPreview_->Voxels.size()
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
