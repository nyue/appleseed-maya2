
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
#include "appleseedmaya/exporters/lightexporter.h"

// Maya headers.
#include <maya/MFnDagNode.h>

// appleseed.renderer headers.
#include "renderer/api/scene.h"

// appleseed.maya headers.
#include "appleseedmaya/exporters/exporterfactory.h"


namespace asf = foundation;
namespace asr = renderer;

void LightExporter::registerExporter()
{
    NodeExporterFactory::registerDagNodeExporter("directionalLight", &LightExporter::create);
    NodeExporterFactory::registerDagNodeExporter("pointLight", &LightExporter::create);
    NodeExporterFactory::registerDagNodeExporter("spotLight", &LightExporter::create);
}

DagNodeExporter *LightExporter::create(
    const MDagPath&                 path,
    asr::Project&                   project,
    AppleseedSession::SessionMode   sessionMode)
{
    return new LightExporter(path, project, sessionMode);
}

LightExporter::LightExporter(
    const MDagPath&                 path,
    asr::Project&                   project,
    AppleseedSession::SessionMode   sessionMode)
  : DagNodeExporter(path, project, sessionMode)
{
}

bool LightExporter::supportsMotionBlur() const
{
    return false;
}

void LightExporter::createEntity()
{
    asr::LightFactoryRegistrar lightFactories;
    const asr::ILightFactory *lightFactory = 0;
    asr::ParamArray lightParams;

    MFnDagNode dagNodeFn(dagPath());

    if(dagNodeFn.typeName() == "directionalLight")
    {
        lightFactory = lightFactories.lookup("directional_light");
    }
    if(dagNodeFn.typeName() == "pointLight")
    {
        lightFactory = lightFactories.lookup("point_light");
    }
    else // spotLight
    {
        lightFactory = lightFactories.lookup("directional_light");
    }

    m_light = lightFactory->create(appleseedName().asChar(), lightParams);

    asf::Matrix4d m = convert(dagPath().inclusiveMatrix());
    asf::Matrix4d invM = convert(dagPath().inclusiveMatrixInverse());
    asf::Transformd xform(m, invM);
    m_light->set_transform(xform);
}

void LightExporter::flushEntity()
{
    mainAssembly().lights().insert(m_light.release());
}
