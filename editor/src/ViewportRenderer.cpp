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

void AppendVoxelOutline(
    std::vector<GPUVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const VoxelCoordinates coordinates,
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

ViewportRenderer::~ViewportRenderer()
{
    Shutdown();
}

bool ViewportRenderer::EnsurePipeline()
{
    if (pipeline_ != nullptr)
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
    const SDL_GPUColorTargetDescription colorDescription{
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
    pipelineInfo.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS;
    pipelineInfo.depth_stencil_state.enable_depth_test = true;
    pipelineInfo.depth_stencil_state.enable_depth_write = true;
    pipelineInfo.target_info.color_target_descriptions = &colorDescription;
    pipelineInfo.target_info.num_color_targets = 1U;
    pipelineInfo.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    pipelineInfo.target_info.has_depth_stencil_target = true;
    pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
    SDL_ReleaseGPUShader(device_, vertexShader);
    SDL_ReleaseGPUShader(device_, fragmentShader);
    if (pipeline_ == nullptr)
    {
        SetError(std::string("Unable to create viewport pipeline: ") + SDL_GetError());
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
    std::optional<VoxelCoordinates> selected,
    std::optional<VoxelCoordinates> addPreview,
    const Vec3 modelCenter) noexcept
{
    if (hovered == selected) hovered.reset();
    if (hoveredHighlight_ == hovered && selectedHighlight_ == selected &&
        addPreviewHighlight_ == addPreview &&
        modelCenter_.X == modelCenter.X && modelCenter_.Y == modelCenter.Y &&
        modelCenter_.Z == modelCenter.Z)
    {
        return;
    }
    hoveredHighlight_ = hovered;
    selectedHighlight_ = selected;
    addPreviewHighlight_ = addPreview;
    modelCenter_ = modelCenter;
    highlightsDirty_ = hoveredHighlight_.has_value() ||
        selectedHighlight_.has_value() || addPreviewHighlight_.has_value();
    if (!highlightsDirty_) ReleaseHighlights();
}

bool ViewportRenderer::EnsureHighlights()
{
    if (!highlightsDirty_) return true;
    std::vector<GPUVertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(24U * 12U * 3U);
    indices.reserve(36U * 12U * 3U);
    if (addPreviewHighlight_)
        AppendVoxelOutline(
            vertices, indices, *addPreviewHighlight_, modelCenter_,
            {0.18F, 1.0F, 0.32F, 1.0F});
    if (hoveredHighlight_)
        AppendVoxelOutline(vertices, indices, *hoveredHighlight_, modelCenter_,
            {1.0F, 0.88F, 0.12F, 1.0F});
    if (selectedHighlight_)
        AppendVoxelOutline(vertices, indices, *selectedHighlight_, modelCenter_,
            {1.0F, 0.38F, 0.08F, 1.0F});
    if (indices.empty())
    {
        highlightsDirty_ = false;
        return true;
    }
    if (!UploadBufferPair(
            vertices.data(), vertices.size() * sizeof(GPUVertex),
            indices.data(), indices.size() * sizeof(std::uint32_t),
            highlightVertexBuffer_, highlightIndexBuffer_,
            "voxel selection highlights"))
    {
        return false;
    }
    highlightIndexCount_ = static_cast<std::uint32_t>(indices.size());
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
    ConfigureHighlights(std::nullopt, std::nullopt, std::nullopt, {});
}

void ViewportRenderer::ReleaseHighlights() noexcept
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
    pipeline_ = nullptr;
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

void ViewportRenderer::SetError(std::string message)
{
    lastError_ = std::move(message);
}

} // namespace VoxelForge::Editor
