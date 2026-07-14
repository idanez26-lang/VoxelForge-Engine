#include "VoxelForge/Renderer/Renderer.h"
#include "VoxelForge/Core/Logger.h"
#include "VoxelForge/Renderer/RendererAPI.h"
#include <string>
namespace VoxelForge::Renderer {
bool Renderer::Initialize(const RendererSpecification& specification){
 if(initialized_){ Core::Logger::Instance().Warning("Renderer initialization was requested more than once."); return true; }
 specification_=specification; statistics_={};
 Core::Logger::Instance().Info("Renderer foundation initialized. Requested API: "+std::string(ToString(specification_.API))+".");
 initialized_=true; return true;
}
void Renderer::Shutdown() noexcept { if(!initialized_) return; initialized_=false; statistics_={}; Core::Logger::Instance().Info("Renderer foundation shut down."); }
void Renderer::BeginFrame() noexcept { if(initialized_) statistics_.ResetPerFrame(); }
void Renderer::EndFrame() noexcept { if(initialized_) ++statistics_.FrameIndex; }
bool Renderer::IsInitialized() noexcept { return initialized_; }
const RendererSpecification& Renderer::GetSpecification() noexcept { return specification_; }
const RendererStatistics& Renderer::GetStatistics() noexcept { return statistics_; }
}
