#include "EditorExitRequest.h"

int main()
{
    VoxelForge::Editor::EditorExitRequest exitRequest;

    if (exitRequest.IsExitRequested())
    {
        return 1;
    }

    exitRequest.RequestExit();

    if (!exitRequest.IsExitRequested())
    {
        return 2;
    }

    if (!exitRequest.ConsumeExitRequest())
    {
        return 3;
    }

    if (exitRequest.IsExitRequested())
    {
        return 4;
    }

    if (exitRequest.ConsumeExitRequest())
    {
        return 5;
    }

    return 0;
}
