#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace VoxelForge::Editor
{

enum class FileDialogStatus
{
    Success,
    Cancelled,
    Error
};

enum class FileDialogKind
{
    ProjectParentFolder,
    ProjectFile,
    ModelFiles
};

struct FileDialogResult final
{
    FileDialogStatus Status = FileDialogStatus::Cancelled;
    FileDialogKind Kind = FileDialogKind::ProjectParentFolder;
    std::filesystem::path Path;
    std::string Error;
    std::vector<std::filesystem::path> Paths;
};

class FileDialogResultChannel final
{
private:
    struct SharedState;

public:
    class Producer final
    {
    public:
        [[nodiscard]] bool Publish(FileDialogResult result) const noexcept;

    private:
        friend class FileDialogResultChannel;
        explicit Producer(std::weak_ptr<SharedState> state);
        std::weak_ptr<SharedState> state_;
    };

    FileDialogResultChannel();
    ~FileDialogResultChannel();

    FileDialogResultChannel(const FileDialogResultChannel&) = delete;
    FileDialogResultChannel& operator=(const FileDialogResultChannel&) = delete;

    [[nodiscard]] Producer CreateProducer() const;
    [[nodiscard]] std::optional<FileDialogResult> Consume();
    void Close() noexcept;

private:
    std::shared_ptr<SharedState> state_;
};

class FileDialogService
{
public:
    virtual ~FileDialogService() = default;

    [[nodiscard]] virtual bool ChooseProjectParentFolder(
        const std::filesystem::path& initialDirectory) = 0;
    [[nodiscard]] virtual bool ChooseProjectFile(
        const std::filesystem::path& initialDirectory) = 0;
    [[nodiscard]] virtual bool ChooseModelFiles(
        const std::filesystem::path& initialDirectory) = 0;
    [[nodiscard]] virtual std::optional<FileDialogResult> ConsumeResult() = 0;
    [[nodiscard]] virtual bool IsPending() const noexcept = 0;
    [[nodiscard]] virtual bool InjectSimulatedResult(FileDialogResult)
    {
        return false;
    }
};

[[nodiscard]] std::unique_ptr<FileDialogService> CreateSDLFileDialogService();
[[nodiscard]] std::unique_ptr<FileDialogService> CreateSimulatedFileDialogService();

} // namespace VoxelForge::Editor
