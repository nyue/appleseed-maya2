
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
#include "appleseedmaya/appleseedsession.h"

// Standard headers.
#include <sstream>
#include <vector>

// Boost headers.
#include "boost/filesystem/path.hpp"
#include "boost/filesystem/convenience.hpp"
#include "boost/filesystem/operations.hpp"
#include "boost/shared_ptr.hpp"
#include "boost/scoped_ptr.hpp"

// Maya headers.
#include <maya/MAnimControl.h>
#include <maya/MComputation.h>
#include <maya/MDagPath.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MGlobal.h>
#include <maya/MItDag.h>
#include <maya/MSelectionList.h>
#include <maya/MObject.h>
#include <maya/MObjectArray.h>

// appleseed.foundation headers.
#include "foundation/platform/timers.h"
#include "foundation/utility/autoreleaseptr.h"
#include "foundation/utility/log.h"
#include "foundation/utility/searchpaths.h"
#include "foundation/utility/stopwatch.h"
#include "foundation/utility/string.h"

// appleseed.renderer headers.
#include "renderer/api/environment.h"
#include "renderer/api/frame.h"
#include "renderer/api/material.h"
#include "renderer/api/project.h"
#include "renderer/api/rendering.h"
#include "renderer/api/scene.h"
#include "renderer/api/utility.h"

// appleseed.maya headers.
#include "appleseedmaya/attributeutils.h"
#include "appleseedmaya/exceptions.h"
#include "appleseedmaya/exporters/dagnodeexporter.h"
#include "appleseedmaya/exporters/exporterfactory.h"
#include "appleseedmaya/exporters/mpxnodeexporter.h"
#include "appleseedmaya/exporters/shapeexporter.h"
#include "appleseedmaya/logger.h"
#include "appleseedmaya/renderercontroller.h"
#include "appleseedmaya/renderglobalsnode.h"
#include "appleseedmaya/utils.h"

namespace bfs = boost::filesystem;
namespace asf = foundation;
namespace asr = renderer;

namespace
{

struct SessionImpl
{
    // Constructor. (IPR or Batch)
    SessionImpl(
        AppleseedSession::SessionMode       mode,
        const AppleseedSession::Options&    options)
      : m_sessionMode(mode)
      , m_options(options)
    {
        createProject();
    }

    // Constructor. (Scene export)
    SessionImpl(
        const MString&                      fileName,
        const AppleseedSession::Options&    options)
      : m_fileName(fileName)
      , m_sessionMode(AppleseedSession::ExportSession)
      , m_options(options)
    {
        m_projectPath = bfs::path(fileName.asChar()).parent_path();

        // Create a dir to store the geom files if it does not exist yet.
        boost::filesystem::path geomPath = m_projectPath / "_geometry";
        if(!boost::filesystem::exists(geomPath))
        {
            if(!boost::filesystem::create_directory(geomPath))
            {
                // todo: throw something here...
            }
        }

        createProject();

        // Set the project filename and add the project directory to the search paths.
        m_project->set_path(m_fileName.asChar());
        m_project->search_paths().set_root_path(m_projectPath.string().c_str());
    }

    // Non-copyable
    SessionImpl(const SessionImpl&);
    SessionImpl& operator=(const SessionImpl&);

    void createProject()
    {
        assert(m_project.get() == 0);

        m_project = asr::ProjectFactory::create("project");
        m_project->add_default_configurations();

        // Insert some config params needed by the interactive renderer.
        asr::Configuration *cfg = m_project->configurations().get_by_name("interactive");
        asr::ParamArray *cfg_params = &cfg->get_parameters();
        cfg_params->insert("sample_renderer", "generic");
        cfg_params->insert("sample_generator", "generic");
        cfg_params->insert("tile_renderer", "generic");
        cfg_params->insert("frame_renderer", "progressive");
        cfg_params->insert("lighting_engine", "pt");
        cfg_params->insert("pixel_renderer", "uniform");
        cfg_params->insert("sampling_mode", "rng");
        cfg_params->insert_path("progressive_frame_renderer.max_fps", "5");

        // Insert some config params needed by the final renderer.
        cfg = m_project->configurations().get_by_name("final");
        cfg_params = &cfg->get_parameters();
        cfg_params->insert("sample_renderer", "generic");
        cfg_params->insert("sample_generator", "generic");
        cfg_params->insert("tile_renderer", "generic");
        cfg_params->insert("frame_renderer", "generic");
        cfg_params->insert("lighting_engine", "pt");
        cfg_params->insert("pixel_renderer", "uniform");
        cfg_params->insert("sampling_mode", "rng");
        cfg_params->insert_path("uniform_pixel_renderer.samples", "16");

        // Create some basic project entities.

        // Create the frame.
        asf::auto_release_ptr<asr::Frame> frame(
            asr::FrameFactory::create("beauty", asr::ParamArray().insert("resolution", "640 480")));
        m_project->set_frame(frame);

        // 16 bits float (half) is the default pixel format in appleseed.
        // Force the pixel format to float to avoid half -> float conversions.
        m_project->get_frame()->get_parameters().insert("pixel_format", "float");

        // Create the scene
        asf::auto_release_ptr<asr::Scene> scene = asr::SceneFactory::create();
        m_project->set_scene(scene);

        // Create the environment.
        asf::auto_release_ptr<asr::Environment> environment(asr::EnvironmentFactory().create("environment", asr::ParamArray()));
        m_project->get_scene()->set_environment(environment);

        // Create the main assembly
        asf::auto_release_ptr<asr::Assembly> assembly = asr::AssemblyFactory().create("assembly", asr::ParamArray());
        m_project->get_scene()->assemblies().insert(assembly);

        // Instance the main assembly
        asf::auto_release_ptr<asr::AssemblyInstance> assemblyInstance = asr::AssemblyInstanceFactory::create("assembly_inst", asr::ParamArray(), "assembly");
        m_project->get_scene()->assembly_instances().insert(assemblyInstance);
    }

    void exportProject()
    {
        MObject defaultRenderGlobalsNode = exportDefaultRenderGlobals();
        MObject appleseedRenderGlobalsNode = exportAppleseedRenderGlobals();

        exportScene();

        // update the frame.
        {
            asr::ParamArray params = m_project->get_frame()->get_parameters();
            params.insert("camera", m_options.m_camera.asChar());

            std::stringstream ss;
            ss << m_options.m_width << " " << m_options.m_height;
            params.insert("resolution", ss.str().c_str());

            // TODO: set crop window here...

            m_project->set_frame(asr::FrameFactory().create("beauty", params));
        }
    }

    void exportScene()
    {
        createExporters();

        // Collect motion blur times.
        MotionBlurTimes motionBlurTimes;
        for(DagExporterMap::const_iterator it = m_dagExporters.begin(), e = m_dagExporters.end(); it != e; ++it)
            it->second->collectMotionBlurSteps(motionBlurTimes);

        // Create appleseed entities.
        for(MPxExporterMap::const_iterator it = m_mpxExporters.begin(), e = m_mpxExporters.end(); it != e; ++it)
            it->second->createEntity(m_options);

        for(DagExporterMap::const_iterator it = m_dagExporters.begin(), e = m_dagExporters.end(); it != e; ++it)
            it->second->createEntity(m_options);

        // For each time step...
        {
            for(DagExporterMap::const_iterator it = m_dagExporters.begin(), e = m_dagExporters.end(); it != e; ++it)
            {
                if(it->second->supportsMotionBlur())
                {
                    it->second->exportCameraMotionStep(0.0f);
                    it->second->exportTransformMotionStep(0.0f);
                    it->second->exportShapeMotionStep(0.0f);
                }
            }
        }

        // Handle auto-instancing.
        if(m_sessionMode != AppleseedSession::ProgressiveRenderSession)
        {
            for(DagExporterMap::const_iterator it = m_dagExporters.begin(), e = m_dagExporters.end(); it != e; ++it)
            {
                const ShapeExporter *shape = dynamic_cast<const ShapeExporter*>(it->second.get());

                if(shape && shape->supportsInstancing())
                {
                    /*
                    hash = exporter->hash();
                    if(instanceMap.find(hash) != end())
                    {
                        DagNodeExporterPtr exporter(shape);
                        // Create InstanceExporter(...)
                        m_dagExporters[it->first] = instanceExporter;
                    }
                    else
                        map[hash] = ShapeExporterPtr(exporter)
                    */
                }
            }
        }

        // Flush entities to the renderer.
        for(MPxExporterMap::const_iterator it = m_mpxExporters.begin(), e = m_mpxExporters.end(); it != e; ++it)
            it->second->flushEntity();

        for(DagExporterMap::const_iterator it = m_dagExporters.begin(), e = m_dagExporters.end(); it != e; ++it)
            it->second->flushEntity();
    }

    MObject exportDefaultRenderGlobals()
    {
        RENDERER_LOG_DEBUG("Exporting default render globals");

        MObject defaultRenderGlobalsNode;
        if(getNodeMObject("defaultRenderGlobals", defaultRenderGlobalsNode))
        {
            MFnDependencyNode defaultGlobalsDepFn(defaultRenderGlobalsNode);
            // ...
        }

        return defaultRenderGlobalsNode;
    }

    MObject exportAppleseedRenderGlobals()
    {
        RENDERER_LOG_DEBUG("Exporting appleseed render globals");

        MObject appleseedRenderGlobalsNode;
        if(getNodeMObject("appleseedRenderGlobals", appleseedRenderGlobalsNode))
        {
            RenderGlobalsNode::applyGlobalsToProject(
                appleseedRenderGlobalsNode,
                *m_project);
        }

        return appleseedRenderGlobalsNode;
    }

    void createExporters()
    {
        RENDERER_LOG_DEBUG("Exporting default material");
        exportDefaultMaterial();

        // Create exporters for all dag nodes in the scene.
        RENDERER_LOG_DEBUG("Creating dag node exporters");
        MDagPath path;
        for(MItDag it(MItDag::kDepthFirst); !it.isDone(); it.next())
        {
            it.getPath(path);
            createDagNodeExporter(path);
        }

        // Collect extra dependency nodes to export.
        RENDERER_LOG_DEBUG("Collecting dependency nodes to export");
        MObjectArray extraObjects;
        for(DagExporterMap::const_iterator it = m_dagExporters.begin(), e = m_dagExporters.end(); it != e; ++it)
            it->second->collectDependencyNodesToExport(extraObjects);

        for(int i = 0, e = extraObjects.length(); i < e; ++i)
            createMPxNodeExporter(extraObjects[i]);
    }

    void exportDefaultMaterial()
    {
        MObject initialShadingGroup;
        if(getNodeMObject("initialShadingGroup", initialShadingGroup))
            createMPxNodeExporter(initialShadingGroup);
    }

    void createMPxNodeExporter(const MObject& object)
    {
        MFnDependencyNode depNodeFn(object);
        if(m_mpxExporters.count(depNodeFn.name()) != 0)
            return;

        MPxNodeExporterPtr exporter;

        try
        {
            exporter.reset(NodeExporterFactory::createMPxNodeExporter(
                object,
                *m_project,
                m_sessionMode));
        }
        catch(const NoExporterForNode&)
        {
            RENDERER_LOG_WARNING(
                "No mpx exporter found for node type %s",
                depNodeFn.typeName().asChar());
            return;
        }

        if(exporter)
        {
            m_mpxExporters[depNodeFn.name()] = exporter;
            RENDERER_LOG_DEBUG(
                "Created mpx exporter for node %s",
                depNodeFn.name().asChar());
        }
    }

    void createDagNodeExporter(const MDagPath& path)
    {
        if(m_dagExporters.count(path.fullPathName()) != 0)
            return;

        MFnDagNode dagNodeFn(path);
        // todo: test here visibility flags, intermediate objects, .., ...?

        DagNodeExporterPtr exporter;

        try
        {
            exporter.reset(NodeExporterFactory::createDagNodeExporter(
                path,
                *m_project,
                m_sessionMode));
        }
        catch(const NoExporterForNode&)
        {
            RENDERER_LOG_WARNING(
                "No dag exporter found for node type %s",
                dagNodeFn.typeName().asChar());
            return;
        }

        if(exporter)
        {
            m_dagExporters[path.fullPathName()] = exporter;
            RENDERER_LOG_DEBUG(
                "Created dag exporter for node %s",
                dagNodeFn.name().asChar());
        }
    }

    bool writeProject() const
    {
        return asr::ProjectFileWriter::write(
            *m_project,
            m_fileName.asChar(),
            asr::ProjectFileWriter::OmitHandlingAssetFiles |
            asr::ProjectFileWriter::OmitWritingGeometryFiles);
    }

    MStatus getNodeMObject(const MString& nodeName, MObject& node)
    {
        MSelectionList selList;
        selList.add(nodeName);

        if(selList.isEmpty())
            return MS::kFailure;

        selList.getDependNode(0, node);
    }

    typedef std::map<MString, MPxNodeExporterPtr, MStringCompareLess> MPxExporterMap;
    typedef std::map<MString, DagNodeExporterPtr, MStringCompareLess> DagExporterMap;

    AppleseedSession::SessionMode               m_sessionMode;
    AppleseedSession::Options                   m_options;
    MTime                                       m_savedTime;

    asf::auto_release_ptr<renderer::Project>    m_project;

    MString                                     m_fileName;
    bfs::path                                   m_projectPath;

    MPxExporterMap                              m_mpxExporters;
    DagExporterMap                              m_dagExporters;

    boost::scoped_ptr<asr::MasterRenderer>      m_renderer;
    RendererController                          m_rendererController;
};

// Global session.
boost::scoped_ptr<SessionImpl> gGlobalSession;

// Plugin path.
bfs::path gPluginPath;

// Saved time.
MTime g_savedTime;

} // unnamed

namespace AppleseedSession
{

MStatus initialize(const MString& pluginPath)
{
    gPluginPath = pluginPath.asChar();
    return MS::kSuccess;
}

MStatus uninitialize()
{
    gGlobalSession.reset();
    return MS::kSuccess;
}

void beginProjectExport(
    const MString& fileName,
    const Options& options)
{
    assert(gGlobalSession.get() == 0);

    g_savedTime = MAnimControl::currentTime();

    if(options.m_sequence)
    {
        std::string fname = fileName.asChar();
        if(fname.find('#') == std::string::npos)
        {
            RENDERER_LOG_ERROR("No frame placeholders in filename.");
            return;
        }

        MComputation computation;
        computation.beginComputation();

        for(int frame = options.m_firstFrame; frame <= options.m_lastFrame; frame += options.m_frameStep)
        {
            if (computation.isInterruptRequested())
            {
                RENDERER_LOG_INFO("Project export aborted.");
                break;
            }

            MGlobal::viewFrame(frame);
            fname = asf::get_numbered_string(fname, frame);
            gGlobalSession.reset(new SessionImpl(fname.c_str(), options));
            gGlobalSession->exportProject();
            gGlobalSession->writeProject();
        }

        computation.endComputation();
    }
    else
    {
        gGlobalSession.reset(new SessionImpl(fileName, options));
        gGlobalSession->exportProject();
        gGlobalSession->writeProject();
    }
}

void beginFinalRender(
    const Options& options)
{
    assert(gGlobalSession.get() == 0);

    g_savedTime = MAnimControl::currentTime();
    gGlobalSession.reset(new SessionImpl(FinalRenderSession, options));
}

void beginProgressiveRender(
    const Options& options)
{
    assert(gGlobalSession.get() == 0);

    g_savedTime = MAnimControl::currentTime();
    gGlobalSession.reset(new SessionImpl(ProgressiveRenderSession, options));
}

void endSession()
{
    assert(gGlobalSession.get());

    gGlobalSession.reset();

    if(g_savedTime != MAnimControl::currentTime())
        MGlobal::viewFrame(g_savedTime);
}

SessionMode sessionMode()
{
    if(gGlobalSession.get() == 0)
        return NoSession;

    return gGlobalSession->m_sessionMode;
}

const Options& options()
{
    assert(gGlobalSession.get());

    return gGlobalSession->m_options;
}

} // namespace AppleseedSession.
