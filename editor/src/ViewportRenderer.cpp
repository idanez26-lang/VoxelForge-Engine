#include "ViewportRenderer.h"
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
    const Voxel::VoxelPalette& palette)
{
    if (!EnsurePipeline() || mesh.Empty() ||
        mesh.VertexCount() > std::numeric_limits<std::uint32_t>::max() ||
        mesh.IndexCount() > std::numeric_limits<std::uint32_t>::max())
    {
        if (mesh.Empty()) SetError("Cannot upload an empty voxel mesh.");
        return false;
    }

    std::vector<GPUVertex> vertices;
    vertices.reserve(mesh.VertexCount());
    std::array<float, 3> minimum = mesh.Vertices().front().Position;
    std::array<float, 3> maximum = minimum;
    for (const Mesh::MeshVertex& source : mesh.Vertices())
    {
        for (std::size_t axis = 0U; axis < 3U; ++axis)
        {
            minimum[axis] = std::min(minimum[axis], source.Position[axis]);
            maximum[axis] = std::max(maximum[axis], source.Position[axis]);
        }
    }
    const std::array<float, 3> center{
        (minimum[0] + maximum[0]) * 0.5F,
        (minimum[1] + maximum[1]) * 0.5F,
        (minimum[2] + maximum[2]) * 0.5F};
    for (const Mesh::MeshVertex& source : mesh.Vertices())
    {
        const Voxel::VoxelColor* color = palette.Get(source.ColorIndex);
        const Voxel::VoxelColor fallback{255U, 0U, 255U, 255U};
        const Voxel::VoxelColor& value = color != nullptr ? *color : fallback;
        vertices.push_back({
            {source.Position[0] - center[0],
             source.Position[1] - center[1],
             source.Position[2] - center[2]},
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

    ClearModel();
    const SDL_GPUBufferCreateInfo vertexInfo{
        SDL_GPU_BUFFERUSAGE_VERTEX, static_cast<std::uint32_t>(vertexBytes), 0U};
    const SDL_GPUBufferCreateInfo indexInfo{
        SDL_GPU_BUFFERUSAGE_INDEX, static_cast<std::uint32_t>(indexBytes), 0U};
    vertexBuffer_ = SDL_CreateGPUBuffer(device_, &vertexInfo);
    indexBuffer_ = SDL_CreateGPUBuffer(device_, &indexInfo);
    const SDL_GPUTransferBufferCreateInfo transferInfo{
        SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        static_cast<std::uint32_t>(vertexBytes + indexBytes), 0U};
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
    if (vertexBuffer_ == nullptr || indexBuffer_ == nullptr || transfer == nullptr)
    {
        if (transfer != nullptr) SDL_ReleaseGPUTransferBuffer(device_, transfer);
        ClearModel();
        SetError(std::string("Unable to create voxel GPU buffers: ") + SDL_GetError());
        return false;
    }

    void* mapped = SDL_MapGPUTransferBuffer(device_, transfer, false);
    if (mapped == nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        ClearModel();
        SetError(std::string("Unable to map voxel upload buffer: ") + SDL_GetError());
        return false;
    }
    std::memcpy(mapped, vertices.data(), vertexBytes);
    std::memcpy(static_cast<unsigned char*>(mapped) + vertexBytes,
        mesh.Indices().data(), indexBytes);
    SDL_UnmapGPUTransferBuffer(device_, transfer);

    SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(device_);
    if (commandBuffer == nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        ClearModel();
        SetError(std::string("Unable to acquire voxel upload command buffer: ") + SDL_GetError());
        return false;
    }
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commandBuffer);
    if (copyPass == nullptr)
    {
        const std::string error = SDL_GetError();
        SDL_CancelGPUCommandBuffer(commandBuffer);
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        ClearModel();
        SetError("Unable to begin voxel upload pass: " + error);
        return false;
    }
    const SDL_GPUTransferBufferLocation vertexSource{transfer, 0U};
    const SDL_GPUBufferRegion vertexDestination{
        vertexBuffer_, 0U, static_cast<std::uint32_t>(vertexBytes)};
    const SDL_GPUTransferBufferLocation indexSource{
        transfer, static_cast<std::uint32_t>(vertexBytes)};
    const SDL_GPUBufferRegion indexDestination{
        indexBuffer_, 0U, static_cast<std::uint32_t>(indexBytes)};
    SDL_UploadToGPUBuffer(copyPass, &vertexSource, &vertexDestination, false);
    SDL_UploadToGPUBuffer(copyPass, &indexSource, &indexDestination, false);
    SDL_EndGPUCopyPass(copyPass);
    if (!SDL_SubmitGPUCommandBuffer(commandBuffer))
    {
        SDL_ReleaseGPUTransferBuffer(device_, transfer);
        ClearModel();
        SetError(std::string("Unable to submit voxel upload: ") + SDL_GetError());
        return false;
    }
    SDL_ReleaseGPUTransferBuffer(device_, transfer);
    indexCount_ = static_cast<std::uint32_t>(mesh.IndexCount());
    lastError_.clear();
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
    const SDL_GPUTextureCreateInfo depthInfo{
        SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREFORMAT_D16_UNORM,
        SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        width, height, 1U, 1U, SDL_GPU_SAMPLECOUNT_1, 0U};
    colorTarget_ = SDL_CreateGPUTexture(device_, &colorInfo);
    depthTarget_ = SDL_CreateGPUTexture(device_, &depthInfo);
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
    const EditorCamera& camera)
{
    if (indexCount_ == 0U || width == 0U || height == 0U ||
        !EnsurePipeline() || !EnsureTargets(width, height))
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
    colorInfo.clear_color = {0.055F, 0.070F, 0.095F, 1.0F};
    colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    colorInfo.store_op = SDL_GPU_STOREOP_STORE;
    colorInfo.cycle = true;
    SDL_GPUDepthStencilTargetInfo depthInfo{};
    depthInfo.texture = depthTarget_;
    depthInfo.clear_depth = 1.0F;
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
    const SDL_GPUBufferBinding vertexBinding{vertexBuffer_, 0U};
    const SDL_GPUBufferBinding indexBinding{indexBuffer_, 0U};
    SDL_BindGPUVertexBuffers(pass, 0U, &vertexBinding, 1U);
    SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);
    SDL_DrawGPUIndexedPrimitives(pass, indexCount_, 1U, 0U, 0, 0U);
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

void ViewportRenderer::SetError(std::string message)
{
    lastError_ = std::move(message);
}

} // namespace VoxelForge::Editor
