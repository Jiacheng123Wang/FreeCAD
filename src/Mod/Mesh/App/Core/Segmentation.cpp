/***************************************************************************
 *   Copyright (c) 2012 Imetric 3D GmbH                                    *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
#include <algorithm>
#endif

#include "Segmentation.h"
#include "Algorithm.h"
#include "Approximation.h"

using namespace MeshCore;

void MeshSurfaceSegment::Initialize(unsigned long)
{
}

void MeshSurfaceSegment::AddFacet(const MeshFacet&)
{
}

void MeshSurfaceSegment::AddSegment(const std::vector<unsigned long>& segm)
{
    if (segm.size() >= minFacets) {
        segments.push_back(segm);
    }
}

// --------------------------------------------------------

MeshDistancePlanarSegment::MeshDistancePlanarSegment(const MeshKernel& mesh, unsigned long minFacets, float tol)
  : MeshDistanceSurfaceSegment(mesh, minFacets, tol), fitter(new PlaneFit)
{
}

MeshDistancePlanarSegment::~MeshDistancePlanarSegment()
{
    delete fitter;
}

void MeshDistancePlanarSegment::Initialize(unsigned long index)
{
    fitter->Clear();

    MeshGeomFacet triangle = kernel.GetFacet(index);
    basepoint = triangle.GetGravityPoint();
    normal = triangle.GetNormal();
    fitter->AddPoint(triangle._aclPoints[0]);
    fitter->AddPoint(triangle._aclPoints[1]);
    fitter->AddPoint(triangle._aclPoints[2]);
}

bool MeshDistancePlanarSegment::TestFacet (const MeshFacet& face) const
{
    if (!fitter->Done())
        fitter->Fit();
    MeshGeomFacet triangle = kernel.GetFacet(face);
    for (int i=0; i<3; i++) {
        if (fabs(fitter->GetDistanceToPlane(triangle._aclPoints[i])) > tolerance)
            return false;
    }

    return true;
}

void MeshDistancePlanarSegment::AddFacet(const MeshFacet& face)
{
    MeshGeomFacet triangle = kernel.GetFacet(face);
    fitter->AddPoint(triangle.GetGravityPoint());
}

// --------------------------------------------------------

bool MeshCurvaturePlanarSegment::TestFacet (const MeshFacet &rclFacet) const
{
    for (int i=0; i<3; i++) {
        const CurvatureInfo& ci = info[rclFacet._aulPoints[i]];
        if (fabs(ci.fMinCurvature) > tolerance)
            return false;
        if (fabs(ci.fMaxCurvature) > tolerance)
            return false;
    }

    return true;
}

bool MeshCurvatureCylindricalSegment::TestFacet (const MeshFacet &rclFacet) const
{
    for (int i=0; i<3; i++) {
        const CurvatureInfo& ci = info[rclFacet._aulPoints[i]];
        if (ci.fMaxCurvature > ci.fMinCurvature) {
            // convexe
            if (fabs(ci.fMinCurvature) > tolerance)
                return false;
            float diff = ci.fMaxCurvature - curvature;
            if (fabs(diff) > tolerance)
                return false;
        }
        else {
            // concave
            if (fabs(ci.fMaxCurvature) > tolerance)
                return false;
            float diff = ci.fMinCurvature + curvature;
            if (fabs(diff) > tolerance)
                return false;
        }
    }

    return true;
}

bool MeshCurvatureSphericalSegment::TestFacet (const MeshFacet &rclFacet) const
{
    for (int i=0; i<3; i++) {
        const CurvatureInfo& ci = info[rclFacet._aulPoints[i]];
        if (ci.fMaxCurvature * ci.fMinCurvature < 0)
            return false;
        float diff;
        diff = fabs(ci.fMinCurvature) - curvature;
        if (fabs(diff) > tolerance)
            return false;
        diff = fabs(ci.fMaxCurvature) - curvature;
        if (fabs(diff) > tolerance)
            return false;
    }

    return true;
}

bool MeshCurvatureFreeformSegment::TestFacet (const MeshFacet &rclFacet) const
{
    for (int i=0; i<3; i++) {
        const CurvatureInfo& ci = info[rclFacet._aulPoints[i]];
        if (fabs(ci.fMinCurvature-c2) > tolerance)
            return false;
        if (fabs(ci.fMaxCurvature-c1) > tolerance)
            return false;
    }

    return true;
}

// --------------------------------------------------------

MeshSurfaceVisitor::MeshSurfaceVisitor (MeshSurfaceSegment& segm, std::vector<unsigned long> &indices)
  : indices(indices), segm(segm)
{
}

MeshSurfaceVisitor::~MeshSurfaceVisitor ()
{
}

bool MeshSurfaceVisitor::AllowVisit (const MeshFacet& face, const MeshFacet&, 
                                     unsigned long, unsigned long, unsigned short)
{
    return segm.TestFacet(face);
}

bool MeshSurfaceVisitor::Visit (const MeshFacet & face, const MeshFacet &,
                                unsigned long ulFInd, unsigned long)
{
    indices.push_back(ulFInd);
    segm.AddFacet(face);
    return true;
}

// --------------------------------------------------------

void MeshSegmentAlgorithm::FindSegments(std::vector<MeshSurfaceSegment*>& segm)
{
    // reset VISIT flags
    unsigned long startFacet;
    MeshCore::MeshAlgorithm cAlgo(myKernel);
    cAlgo.ResetFacetFlag(MeshCore::MeshFacet::VISIT);

    const MeshCore::MeshFacetArray& rFAry = myKernel.GetFacets();
    MeshCore::MeshFacetArray::_TConstIterator iCur = rFAry.begin();
    MeshCore::MeshFacetArray::_TConstIterator iBeg = rFAry.begin();
    MeshCore::MeshFacetArray::_TConstIterator iEnd = rFAry.end();

    // start from the first not visited facet
    cAlgo.CountFacetFlag(MeshCore::MeshFacet::VISIT);
    std::vector<unsigned long> resetVisited;

    for (std::vector<MeshSurfaceSegment*>::iterator it = segm.begin(); it != segm.end(); ++it) {
        cAlgo.ResetFacetsFlag(resetVisited, MeshCore::MeshFacet::VISIT);
        resetVisited.clear();

        iCur = std::find_if(iBeg, iEnd, std::bind2nd(MeshCore::MeshIsNotFlag<MeshCore::MeshFacet>(),
            MeshCore::MeshFacet::VISIT));
        startFacet = iCur - iBeg;
        while (startFacet != ULONG_MAX) {
            // collect all facets of the same geometry
            std::vector<unsigned long> indices;
            indices.push_back(startFacet);
            (*it)->Initialize(startFacet);
            MeshSurfaceVisitor pv(**it, indices);
            myKernel.VisitNeighbourFacets(pv, startFacet);

            // add or discard the segment
            if (indices.size() == 1) {
                resetVisited.push_back(startFacet);
            }
            else {
                (*it)->AddSegment(indices);
            }

            // search for the next start facet
            iCur = std::find_if(iCur, iEnd, std::bind2nd(MeshCore::MeshIsNotFlag<MeshCore::MeshFacet>(),
                MeshCore::MeshFacet::VISIT));
            if (iCur < iEnd)
                startFacet = iCur - iBeg;
            else
                startFacet = ULONG_MAX;
        }
    }
}
