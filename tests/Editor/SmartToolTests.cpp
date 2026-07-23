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

        tool.SetGeometry(SmartGeometry::Pencil);
        tool.Brush().Shape = SmartBrushShape::Cube;
        if (ResolveSmartBrushShape(tool.Geometry(), tool.Brush().Shape) !=
            SmartBrushShape::Cube)
            throw std::runtime_error("cube shape");
        tool.Brush().Shape = SmartBrushShape::Sphere;
        if (ResolveSmartBrushShape(tool.Geometry(), tool.Brush().Shape) !=
            SmartBrushShape::Sphere)
            throw std::runtime_error("sphere shape");

        tool.SetGeometry(SmartGeometry::Cube);
        if (!tool.IsOperational() || ResolveSmartBrushShape(
                tool.Geometry(), tool.Brush().Shape) != SmartBrushShape::Cube)
            throw std::runtime_error("cube geometry");
        tool.SetGeometry(SmartGeometry::Sphere);
        if (!tool.IsOperational() || ResolveSmartBrushShape(
                tool.Geometry(), tool.Brush().Shape) != SmartBrushShape::Sphere)
            throw std::runtime_error("sphere geometry");

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
        if (tool.IsOperational() || tool.Geometry() != SmartGeometry::Face)
            throw std::runtime_error("future geometry");

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
