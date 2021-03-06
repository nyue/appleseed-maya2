
//
// This source file is part of appleseed.
// Visit http://appleseedhq.net/ for additional information and resources.
//
// This software is released under the MIT license.
//
// Copyright (c) 2016 Esteban Tovagliari, The appleseedhq Organization
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

// Interface header.
#include "appleseedmaya/shadingnodetemplatebuilder.h"

// Standard headers.
#include <iostream>
#include <sstream>

// Maya headers.
#include <maya/MGlobal.h>

// appleseed.maya headers.
#include <appleseedmaya/shadingnoderegistry.h>

ShadingNodeTemplateBuilder::ShadingNodeTemplateBuilder(const OSLShaderInfo& shaderInfo)
{
    std::stringstream ss;
    ss << "global proc AE" << shaderInfo.mayaName << "Template(string $nodeName)\n";
    ss << "{\n";
    ss << "    AEswatchDisplay $nodeName;\n"; // <-- this crashes Maya.
    ss << "    editorTemplate -beginScrollLayout;\n";
    /*
                editorTemplate -beginLayout "Group name" -collapse 0;
                    editorTemplate -addControl "...";
                editorTemplate -endLayout;
    */
	ss << "    AEdependNodeTemplate $nodeName;\n";
	ss << "    editorTemplate -addExtraControls;\n";
	ss << "    editorTemplate -endScrollLayout;\n";
    ss << "}\n";

    m_melTemplate = ss.str().c_str();
}

MStatus ShadingNodeTemplateBuilder::registerAETemplate() const
{
    return MGlobal::executeCommand(m_melTemplate);
}

void ShadingNodeTemplateBuilder::logAETemplate() const
{
    std::cout << m_melTemplate << std::endl;
}
