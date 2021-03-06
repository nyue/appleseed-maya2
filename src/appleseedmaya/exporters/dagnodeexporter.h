
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

#ifndef APPLESEED_MAYA_EXPORTERS_DAGNODEEXPORTER_H
#define APPLESEED_MAYA_EXPORTERS_DAGNODEEXPORTER_H

// Maya headers.
#include <maya/MDagPath.h>
#include <maya/MMatrix.h>
#include <maya/MObjectArray.h>

// appleseed.foundation headers.
#include "foundation/math/matrix.h"

// appleseed.maya headers.
#include "appleseedmaya/exporters/mpxnodeexporter.h"

// Forward declarations.
class MotionBlurTimes;

class DagNodeExporter
  : public MPxNodeExporter
{
  public:

    virtual MString appleseedName() const;

    // Collect dependency nodes to export (materials, ...).
    virtual void collectDependencyNodesToExport(MObjectArray& nodes);

    // Returns true if the entity created by this exporter can be motion blurred.
    virtual bool supportsMotionBlur() const;

    // Motion blur.
    virtual void collectMotionBlurSteps(MotionBlurTimes& motionTimes) const;
    virtual void exportCameraMotionStep(float time);
    virtual void exportTransformMotionStep(float time);
    virtual void exportShapeMotionStep(float time);

  protected:

    DagNodeExporter(
      const MDagPath&               path,
      renderer::Project&            project,
      AppleseedSession::SessionMode sessionMode);

    const MDagPath& dagPath() const;

    foundation::Matrix4d convert(const MMatrix& m) const;

  private:
    MDagPath m_path;
};

typedef boost::shared_ptr<DagNodeExporter> DagNodeExporterPtr;

#endif  // !APPLESEED_MAYA_EXPORTERS_DAGNODEEXPORTER_H
