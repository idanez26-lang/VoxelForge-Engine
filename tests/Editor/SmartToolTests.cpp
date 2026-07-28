#include "SmartTools/SmartTool.h"

#include <iostream>
#include <stdexcept>

using namespace VoxelForge::Editor;

int main()
{
    try
    {
        SmartTool tool;
        if (tool.Geometry() != SmartGeometry::Pencil ||
            tool.Mode() != SmartToolMode::SingleVoxel ||
            tool.Action() != SmartAction::Add ||
            !tool.IsOperational())
            throw std::runtime_error("defaults");

        tool.Brush().Shape = SmartBrushShape::Sphere;
        tool.Brush().Dimension = SmartBrushDimension::Surface2D;
        tool.Brush().Orientation = SmartBrushOrientation::Z;
        tool.Brush().Size = 7;
        tool.Brush().PaletteIndex = 23U;
        tool.SetStatistics(10U, 4U, 5U, 1U);
        tool.SetPreview(SmartToolPreviewState::Valid);
        tool.SetAction(SmartAction::Paint);
        if (!tool.IsOperational() ||
            tool.Brush().Shape != SmartBrushShape::Sphere ||
            tool.Brush().PaletteIndex != 23U ||
            tool.Statistics().Available ||
            tool.Preview().State != SmartToolPreviewState::Unavailable)
            throw std::runtime_error("paint transition");

        tool.SetAction(SmartAction::Erase);
        if (!tool.IsOperational() || tool.Brush().Size != 7)
            throw std::runtime_error("erase transition");

        tool.SetMode(SmartToolMode::CubeBrush);
        if (tool.Geometry() != SmartGeometry::Pencil ||
            tool.Mode() != SmartToolMode::CubeBrush ||
            tool.Brush().Size != 7 || tool.Brush().PaletteIndex != 23U ||
            !tool.IsOperational())
            throw std::runtime_error("cube brush transition");
        tool.SetMode(SmartToolMode::SingleVoxel);
        if (tool.Geometry() != SmartGeometry::Pencil ||
            tool.Mode() != SmartToolMode::SingleVoxel ||
            tool.Brush().Size != 7 || tool.Brush().PaletteIndex != 23U ||
            !tool.IsOperational())
            throw std::runtime_error("single voxel return transition");
        tool.SetMode(SmartToolMode::SphereBrush);
        if (tool.Geometry() != SmartGeometry::Pencil ||
            tool.Mode() != SmartToolMode::SphereBrush ||
            tool.Brush().Size != 7 || tool.Brush().PaletteIndex != 23U ||
            !tool.IsOperational())
            throw std::runtime_error("sphere brush transition");
        tool.SetMode(SmartToolMode::CylinderBrush);
        if (tool.Geometry() != SmartGeometry::Pencil ||
            tool.Mode() != SmartToolMode::CylinderBrush ||
            tool.Brush().Size != 7 || tool.Brush().PaletteIndex != 23U ||
            !tool.IsOperational())
            throw std::runtime_error("cylinder brush transition");

        tool.SetAction(SmartAction::Add);
        if (!tool.IsOperational()) throw std::runtime_error("create action");
        tool.SetAction(SmartAction::Paint);
        if (!tool.IsOperational()) throw std::runtime_error("paint action");
        tool.SetAction(SmartAction::Erase);
        if (!tool.IsOperational()) throw std::runtime_error("remove action");
        for (const SmartGeometry unsupported :
            {SmartGeometry::Cube, SmartGeometry::Sphere})
        {
            tool.SetGeometry(unsupported);
            if (tool.IsOperational())
                throw std::runtime_error("removed smart geometry remains operational");
        }
        tool.SetGeometry(SmartGeometry::Pencil);
        tool.SetGeometry(SmartGeometry::Geometry);
        if (!tool.IsOperational() ||
            tool.Geometry() != SmartGeometry::Geometry)
            throw std::runtime_error("geometry transition");
        tool.SetGeometry(SmartGeometry::Pencil);
        tool.SetGeometry(SmartGeometry::Fill);
        tool.SetFillMode(SmartFillMode::Plane);
        if (!tool.IsOperational() ||
            tool.Geometry() != SmartGeometry::Fill ||
            tool.FillMode() != SmartFillMode::Plane)
            throw std::runtime_error("fill mode transition");
        tool.SetFillMode(SmartFillMode::Connected);
        tool.SetGeometry(SmartGeometry::Pencil);

        SmartBrushSizeFeedback feedback;
        if (feedback.IsVisible(0U))
            throw std::runtime_error("feedback initial visibility");
        feedback.Rearm(6, SmartBrushShape::Cube, SmartAction::Paint, 100U);
        if (!feedback.IsVisible(100U) || !feedback.IsVisible(1349U) ||
            feedback.IsVisible(1350U) || feedback.Size() != 6 ||
            feedback.Shape() != SmartBrushShape::Cube ||
            feedback.Action() != SmartAction::Paint)
            throw std::runtime_error("feedback timing or metadata");
        feedback.Rearm(9, SmartBrushShape::Sphere, SmartAction::Erase, 900U);
        if (!feedback.IsVisible(2149U) || feedback.IsVisible(2150U) ||
            feedback.Size() != 9 || feedback.Shape() != SmartBrushShape::Sphere ||
            feedback.Action() != SmartAction::Erase)
            throw std::runtime_error("feedback rearm");

        tool.SetGeometry(SmartGeometry::Face);
        if (!tool.IsOperational() || tool.Geometry() != SmartGeometry::Face ||
            tool.Brush().Size != 7 || tool.Brush().PaletteIndex != 23U)
            throw std::runtime_error("face geometry transition");

        tool.SetStatistics(10U, 4U, 5U, 1U);
        tool.SetPreview(SmartToolPreviewState::Valid);
        if (!tool.Statistics().Available ||
            tool.Preview().State != SmartToolPreviewState::Valid)
            throw std::runtime_error("preview stats");

        std::cout << "Smart Tool tests passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
