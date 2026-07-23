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

        tool.SetGeometry(SmartGeometry::Cube);
        if (!tool.IsOperational() || ResolveSmartBrushShape(
                tool.Geometry(), tool.Brush().Shape) != SmartBrushShape::Cube)
            throw std::runtime_error("cube geometry");
        tool.SetGeometry(SmartGeometry::Sphere);
        if (!tool.IsOperational() || ResolveSmartBrushShape(
                tool.Geometry(), tool.Brush().Shape) != SmartBrushShape::Sphere)
            throw std::runtime_error("sphere geometry");

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
