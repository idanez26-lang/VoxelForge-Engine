#pragma once

// VF-STAB-01 bug 4 — RAII guard for the SHARED voxel-edit reentrancy flag.
//
// StampPreviewController::Place() (and other edit entry points) set a bool&
// flag to mark an edit in progress, then must clear it. Clearing it by hand on
// the normal return path leaks `true` if anything between set and clear throws
// (e.g. std::bad_alloc while building a large placement plan), which then
// permanently blocks every future edit that tests the same shared flag. This
// guard sets the flag on construction and restores it to false on destruction —
// on normal scope exit AND during stack unwinding — without swallowing the
// exception, which keeps propagating past the guard.

namespace VoxelForge::Editor
{

class ScopedEditInProgress final
{
public:
    explicit ScopedEditInProgress(bool& flag) noexcept : flag_(flag)
    {
        flag_ = true;
    }

    ~ScopedEditInProgress() noexcept { flag_ = false; }

    ScopedEditInProgress(const ScopedEditInProgress&) = delete;
    ScopedEditInProgress& operator=(const ScopedEditInProgress&) = delete;
    ScopedEditInProgress(ScopedEditInProgress&&) = delete;
    ScopedEditInProgress& operator=(ScopedEditInProgress&&) = delete;

private:
    bool& flag_;
};

} // namespace VoxelForge::Editor
