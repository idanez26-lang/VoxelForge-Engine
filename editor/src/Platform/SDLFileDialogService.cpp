#include "Platform/FileDialogService.h"

#include <SDL3/SDL.h>

#include <deque>
#include <mutex>
#include <utility>

namespace VoxelForge::Editor
{

struct FileDialogResultChannel::SharedState final
{
    std::mutex Mutex;
    std::deque<FileDialogResult> Results;
    bool Accepting = true;
};

FileDialogResultChannel::Producer::Producer(
    std::weak_ptr<SharedState> state)
    : state_(std::move(state))
{
}

bool FileDialogResultChannel::Producer::Publish(
    FileDialogResult result) const noexcept
{
    try
    {
        const std::shared_ptr<SharedState> state = state_.lock();
        if (!state)
        {
            return false;
        }

        std::scoped_lock lock(state->Mutex);
        if (!state->Accepting)
        {
            return false;
        }
        state->Results.push_back(std::move(result));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

FileDialogResultChannel::FileDialogResultChannel()
    : state_(std::make_shared<SharedState>())
{
}

FileDialogResultChannel::~FileDialogResultChannel()
{
    Close();
}

FileDialogResultChannel::Producer FileDialogResultChannel::CreateProducer() const
{
    return Producer(state_);
}

std::optional<FileDialogResult> FileDialogResultChannel::Consume()
{
    if (!state_)
    {
        return std::nullopt;
    }
    std::scoped_lock lock(state_->Mutex);
    if (state_->Results.empty())
    {
        return std::nullopt;
    }
    FileDialogResult result = std::move(state_->Results.front());
    state_->Results.pop_front();
    return result;
}

void FileDialogResultChannel::Close() noexcept
{
    if (!state_)
    {
        return;
    }
    {
        std::scoped_lock lock(state_->Mutex);
        state_->Accepting = false;
        state_->Results.clear();
    }
    state_.reset();
}

namespace
{
std::string ToUtf8(const std::filesystem::path& path)
{
    const std::u8string value = path.u8string();
    return std::string(value.begin(), value.end());
}

std::filesystem::path FromUtf8(const std::string_view value)
{
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(value.data()), value.size()));
}

struct DialogContext final
{
    FileDialogResultChannel::Producer Producer;
    FileDialogKind Kind;
    std::string InitialDirectory;
    SDL_DialogFileFilter Filter{};
};

void SDLCALL OnDialogComplete(
    void* userData,
    const char* const* fileList,
    int)
{
    std::unique_ptr<DialogContext> context(
        static_cast<DialogContext*>(userData));
    try
    {
        FileDialogResult result;
        result.Kind = context->Kind;

        if (fileList == nullptr)
        {
            result.Status = FileDialogStatus::Error;
            result.Error = SDL_GetError();
            if (result.Error.empty())
            {
                result.Error = "The system file dialog failed.";
            }
        }
        else if (fileList[0] == nullptr)
        {
            result.Status = FileDialogStatus::Cancelled;
        }
        else
        {
            result.Status = FileDialogStatus::Success;
            result.Path = FromUtf8(fileList[0]);
            for (std::size_t index = 0U; fileList[index] != nullptr; ++index)
            {
                result.Paths.push_back(FromUtf8(fileList[index]));
            }
        }

        static_cast<void>(context->Producer.Publish(std::move(result)));
    }
    catch (...)
    {
        // No C++ exception may cross SDL's C callback boundary. Failure to
        // allocate an error result is also safely reduced to no publication.
        try
        {
            static_cast<void>(context->Producer.Publish({
                FileDialogStatus::Error,
                context->Kind,
                {},
                "Unable to copy the system file dialog result.",
                {}}));
        }
        catch (...)
        {
        }
    }
}

class SDLFileDialogService final : public FileDialogService
{
public:
    ~SDLFileDialogService() override
    {
        channel_.Close();
    }

    bool ChooseProjectParentFolder(
        const std::filesystem::path& initialDirectory) override
    {
        if (pending_)
        {
            return false;
        }
        auto context = std::make_unique<DialogContext>(DialogContext{
            channel_.CreateProducer(), FileDialogKind::ProjectParentFolder,
            ToUtf8(initialDirectory), {}});
        DialogContext* rawContext = context.release();
        pending_ = true;
        SDL_ShowOpenFolderDialog(
            OnDialogComplete,
            rawContext,
            nullptr,
            rawContext->InitialDirectory.empty()
                ? nullptr : rawContext->InitialDirectory.c_str(),
            false);
        return true;
    }

    bool ChooseProjectFile(
        const std::filesystem::path& initialDirectory) override
    {
        if (pending_)
        {
            return false;
        }
        auto context = std::make_unique<DialogContext>(DialogContext{
            channel_.CreateProducer(), FileDialogKind::ProjectFile,
            ToUtf8(initialDirectory), {"VoxelForge Project", "vfproject"}});
        DialogContext* rawContext = context.release();
        pending_ = true;
        SDL_ShowOpenFileDialog(
            OnDialogComplete,
            rawContext,
            nullptr,
            &rawContext->Filter,
            1,
            rawContext->InitialDirectory.empty()
                ? nullptr : rawContext->InitialDirectory.c_str(),
            false);
        return true;
    }

    bool ChooseModelFiles(
        const std::filesystem::path& initialDirectory) override
    {
        if (pending_)
        {
            return false;
        }
        auto context = std::make_unique<DialogContext>(DialogContext{
            channel_.CreateProducer(), FileDialogKind::ModelFiles,
            ToUtf8(initialDirectory), {"MagicaVoxel Model", "vox"}});
        DialogContext* rawContext = context.release();
        pending_ = true;
        SDL_ShowOpenFileDialog(
            OnDialogComplete,
            rawContext,
            nullptr,
            &rawContext->Filter,
            1,
            rawContext->InitialDirectory.empty()
                ? nullptr : rawContext->InitialDirectory.c_str(),
            true);
        return true;
    }

    std::optional<FileDialogResult> ConsumeResult() override
    {
        std::optional<FileDialogResult> result = channel_.Consume();
        if (result)
        {
            pending_ = false;
        }
        return result;
    }

    bool IsPending() const noexcept override
    {
        return pending_;
    }

private:
    FileDialogResultChannel channel_;
    bool pending_ = false;
};

class SimulatedFileDialogService final : public FileDialogService
{
public:
    bool ChooseProjectParentFolder(const std::filesystem::path&) override
    {
        return Begin(FileDialogKind::ProjectParentFolder);
    }
    bool ChooseProjectFile(const std::filesystem::path&) override
    {
        return Begin(FileDialogKind::ProjectFile);
    }
    bool ChooseModelFiles(const std::filesystem::path&) override
    {
        return Begin(FileDialogKind::ModelFiles);
    }
    std::optional<FileDialogResult> ConsumeResult() override
    {
        auto result = channel_.Consume();
        if (result) pending_ = false;
        return result;
    }
    bool IsPending() const noexcept override { return pending_; }
    bool InjectSimulatedResult(FileDialogResult result) override
    {
        if (!pending_ || result.Kind != pendingKind_) return false;
        return channel_.CreateProducer().Publish(std::move(result));
    }

private:
    bool Begin(const FileDialogKind kind)
    {
        if (pending_) return false;
        pending_ = true;
        pendingKind_ = kind;
        return true;
    }
    FileDialogResultChannel channel_;
    FileDialogKind pendingKind_ = FileDialogKind::ProjectParentFolder;
    bool pending_ = false;
};
}

std::unique_ptr<FileDialogService> CreateSDLFileDialogService()
{
    return std::make_unique<SDLFileDialogService>();
}

std::unique_ptr<FileDialogService> CreateSimulatedFileDialogService()
{
    return std::make_unique<SimulatedFileDialogService>();
}

} // namespace VoxelForge::Editor
