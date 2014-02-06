// ======================================================================== //
// Copyright 2009-2013 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "bezier1i.h"
#include "common/ray.h"
#include "geometry/filter.h"

namespace embree
{
  struct BezierCurve3D
  {
    Vec3fa v0,v1,v2,v3;
    float t0,t1;
    int depth;

    __forceinline BezierCurve3D() {}

    __forceinline BezierCurve3D(const Vec3fa& v0, 
                                const Vec3fa& v1, 
                                const Vec3fa& v2, 
                                const Vec3fa& v3,
                                const float t0,
                                const float t1,
                                const int depth)
      : v0(v0), v1(v1), v2(v2), v3(v3), t0(t0), t1(t1), depth(depth) {}

    __forceinline const BBox3fa bounds() const {
      BBox3fa b = merge(BBox3fa(v0),BBox3fa(v1),BBox3fa(v2),BBox3fa(v3));
      return enlarge(b,Vec3fa(b.upper.w));
    }

    __forceinline void subdivide(BezierCurve3D& left, BezierCurve3D& right) const
    {
      const Vec3fa p00 = v0;
      const Vec3fa p01 = v1;
      const Vec3fa p02 = v2;
      const Vec3fa p03 = v3;

      const Vec3fa p10 = (p00 + p01) * 0.5f;
      const Vec3fa p11 = (p01 + p02) * 0.5f;
      const Vec3fa p12 = (p02 + p03) * 0.5f;
      const Vec3fa p20 = (p10 + p11) * 0.5f;
      const Vec3fa p21 = (p11 + p12) * 0.5f;
      const Vec3fa p30 = (p20 + p21) * 0.5f;

      const float t01 = (t0 + t1) * 0.5f;

      left.v0 = p00;
      left.v1 = p10;
      left.v2 = p20;
      left.v3 = p30;
      left.t0 = t0;
      left.t1 = t01;
      left.depth = depth-1;
        
      right.v0 = p30;
      right.v1 = p21;
      right.v2 = p12;
      right.v3 = p03;
      right.t0 = t01;
      right.t1 = t1;
      right.depth = depth-1;
    }

    __forceinline void eval(const float t, Vec3fa& point, Vec3fa& tangent)
    {
      const float t0 = 1.0f - t, t1 = t;

      const Vec3fa p00 = v0;
      const Vec3fa p01 = v1;
      const Vec3fa p02 = v2;
      const Vec3fa p03 = v3;

      const Vec3fa p10 = p00 * t0 + p01 * t1;
      const Vec3fa p11 = p01 * t0 + p02 * t1;
      const Vec3fa p12 = p02 * t0 + p03 * t1;
      const Vec3fa p20 = p10 * t0 + p11 * t1;
      const Vec3fa p21 = p11 * t0 + p12 * t1;
      const Vec3fa p30 = p20 * t0 + p21 * t1;

      point = p30;
      tangent = p21-p20;
    }

    __forceinline avx4f eval(const avxf& c0, const avxf& c1, const avxf& c2, const avxf& c3)
    {
      const avx4f p00 = avx4f(v0);
      const avx4f p01 = avx4f(v1);
      const avx4f p02 = avx4f(v2);
      const avx4f p03 = avx4f(v3);
      return c0*p00 + c1*p01 + c2*p02 + c3*p03; // FIXME: use fmadd
    }

    friend inline std::ostream& operator<<(std::ostream& cout, const BezierCurve3D& curve) {
      return cout << "{ v0 = " << curve.v0 << ", v1 = " << curve.v1 << ", v2 = " << curve.v2 << ", v3 = " << curve.v3 << ", depth = " << curve.depth << " }";
    }
  };

  __forceinline bool intersect_box(const BBox3fa& box, const Ray& ray)
  {
#if 0
    const float clipNearX = (box.lower.x - ray.org.x) / ray.dir.x; // FIXME: use rdir
    const float clipNearY = (box.lower.y - ray.org.y) / ray.dir.y; // FIXME: use SSE for intersection
    const float clipNearZ = (box.lower.z - ray.org.z) / ray.dir.z;
    const float clipFarX = (box.upper.x - ray.org.x) / ray.dir.x;
    const float clipFarY = (box.upper.y - ray.org.y) / ray.dir.y;
    const float clipFarZ = (box.upper.z - ray.org.z) / ray.dir.z;

    const float near = max(max(min(clipNearX, clipFarX), min(clipNearY, clipFarY)), min(clipNearZ, clipFarZ));
    const float far   = min(min(max(clipNearX, clipFarX), max(clipNearY, clipFarY)), max(clipNearZ, clipFarZ));
    const bool hit    = max(near,ray.tnear) <= min(far,ray.tfar);
    dist = near;
    return hit;
#else
    return max(box.lower.x,box.lower.y) <= 0.0f && 0.0f <= min(box.upper.x,box.upper.y);
#endif
  }

#if 1

  /*! Intersector for a single ray with a bezier curve. */
  struct Bezier1iIntersector1
  {
    typedef Bezier1i Primitive;

    struct Precalculations 
    {
      __forceinline Precalculations (const Ray& ray)
      : ray_space(rcp(frame(ray.dir))) {}

      LinearSpace3f ray_space;
    };

    static __forceinline void intersect(const Precalculations& pre, Ray& ray, const Bezier1i& curve_in, const void* geom)
    {
      /* load bezier curve control points */
      STAT3(normal.trav_prims,1,1,1);
      const Vec3fa v0 = curve_in.p[0];
      const Vec3fa v1 = curve_in.p[1];
      const Vec3fa v2 = curve_in.p[2];
      const Vec3fa v3 = curve_in.p[3];

      /* transform control points into ray space */
      Vec3fa w0 = xfmVector(pre.ray_space,v0-ray.org); w0.w = v0.w;
      Vec3fa w1 = xfmVector(pre.ray_space,v1-ray.org); w1.w = v1.w;
      Vec3fa w2 = xfmVector(pre.ray_space,v2-ray.org); w2.w = v2.w;
      Vec3fa w3 = xfmVector(pre.ray_space,v3-ray.org); w3.w = v3.w;
      BezierCurve3D curve2D(w0,w1,w2,w3,0.0f,1.0f,4);

      /* subdivide 3 levels at once */ 
      const avx4f p0 = curve2D.eval(coeff0[0],coeff0[1],coeff0[2],coeff0[3]);
      const avx4f p1 = curve2D.eval(coeff1[0],coeff1[1],coeff1[2],coeff1[3]);

      /* approximative intersection with cone */
      const avx4f v = p1-p0;
      const avx4f w = -p0;
      const avxf d0 = w.x*v.x + w.y*v.y;
      const avxf d1 = v.x*v.x + v.y*v.y;
      const avxf u = clamp(d0/d1,avxf(zero),avxf(one));
      const avx4f p = p0 + u*v;
      const avxf t = p.z;
      const avxf d2 = p.x*p.x + p.y*p.y; 
      const avxf r = max(p.w,ray.org.w+ray.dir.w*t);
      const avxf r2 = r*r;
      avxb valid = d2 <= r2 & avxf(ray.tnear) < t & t < avxf(ray.tfar);
    retry:
      if (unlikely(none(valid))) return;
      const float one_over_8 = 1.0f/8.0f;
      size_t i = select_min(valid,t);

      /* intersection filter test */
#if defined(__INTERSECTION_FILTER__)
      int geomID = curve_in.geomID;
      Geometry* geometry = ((Scene*)geom)->get(geomID);
      if (!likely(geometry->hasIntersectionFilter1())) 
      {
#endif
        /* update hit information */
        const float uu = (float(i)+u[i])*one_over_8;
        BezierCurve3D curve3D(v0,v1,v2,v3,0.0f,1.0f,0);
        Vec3fa P,T; curve3D.eval(uu,P,T);
        if (T == Vec3fa(zero)) { valid[i] = 0; goto retry; } // ignore denormalized curves
        ray.u = uu;
        ray.v = 0.0f;
        ray.tfar = t[i];
        ray.Ng = T;
        ray.geomID = curve_in.geomID;
        ray.primID = curve_in.primID;
#if defined(__INTERSECTION_FILTER__)
          return;
      }

      while (true) 
      {
        const float uu = (float(i)+u[i])*one_over_8;
        BezierCurve3D curve3D(v0,v1,v2,v3,0.0f,1.0f,0);
        Vec3fa P,T; curve3D.eval(uu,P,T);
        if (runIntersectionFilter1(geometry,ray,uu,0.0f,t[i],T,geomID,curve_in.primID)) return;
        valid[i] = 0;
        if (none(valid)) return;
        i = select_min(valid,t);
      }
#endif
    }

    static __forceinline void intersect(const Precalculations& pre, Ray& ray, const Bezier1i* curves, size_t num, void* geom)
    {
      for (size_t i=0; i<num; i++)
        intersect(pre,ray,curves[i],geom);
    }

    static __forceinline bool occluded(const Precalculations& pre, Ray& ray, const Bezier1i& curve_in, const void* geom) 
    {
      /* load bezier curve control points */
      STAT3(normal.trav_prims,1,1,1);
      const Vec3fa v0 = curve_in.p[0];
      const Vec3fa v1 = curve_in.p[1];
      const Vec3fa v2 = curve_in.p[2];
      const Vec3fa v3 = curve_in.p[3];

      /* transform control points into ray space */
      Vec3fa w0 = xfmVector(pre.ray_space,v0-ray.org); w0.w = v0.w;
      Vec3fa w1 = xfmVector(pre.ray_space,v1-ray.org); w1.w = v1.w;
      Vec3fa w2 = xfmVector(pre.ray_space,v2-ray.org); w2.w = v2.w;
      Vec3fa w3 = xfmVector(pre.ray_space,v3-ray.org); w3.w = v3.w;
      BezierCurve3D curve2D(w0,w1,w2,w3,0.0f,1.0f,4);

      /* subdivide 3 levels at once */ 
      const avx4f p0 = curve2D.eval(coeff0[0],coeff0[1],coeff0[2],coeff0[3]);
      const avx4f p1 = curve2D.eval(coeff1[0],coeff1[1],coeff1[2],coeff1[3]);

      /* approximative intersection with cone */
      const avx4f v = p1-p0;
      const avx4f w = -p0;
      const avxf d0 = w.x*v.x + w.y*v.y;
      const avxf d1 = v.x*v.x + v.y*v.y;
      const avxf u = clamp(d0/d1,avxf(zero),avxf(one));
      const avx4f p = p0 + u*v;
      const avxf t = p.z;
      const avxf d2 = p.x*p.x + p.y*p.y; 
      const avxf r = p.w+ray.org.w+ray.dir.w*t;
      const avxf r2 = r*r;
      avxb valid = d2 <= r2 & avxf(ray.tnear) < t & t < avxf(ray.tfar);
      if (none(valid)) return false;

      /* intersection filter test */
#if defined(__INTERSECTION_FILTER__)

      size_t i = select_min(valid,t);
      int geomID = curve_in.geomID;
      Geometry* geometry = ((Scene*)geom)->get(geomID);
      if (likely(!geometry->hasOcclusionFilter1())) return true;
      const float one_over_8 = 1.0f/8.0f;

      while (true) 
      {
        /* calculate hit information */
        const float uu = (float(i)+u[i])*one_over_8;
        BezierCurve3D curve3D(v0,v1,v2,v3,0.0f,1.0f,0);
        Vec3fa P,T; curve3D.eval(uu,P,T);
        if (runOcclusionFilter1(geometry,ray,uu,0.0f,t[i],T,geomID,curve_in.primID)) break;
        valid[i] = 0;
        if (none(valid)) return false;
        i = select_min(valid,t);
      }
#endif
      return true;
    }

    static __forceinline bool occluded(const Precalculations& pre, Ray& ray, const Bezier1i* curves, size_t num, void* geom) 
    {
      for (size_t i=0; i<num; i++) 
        if (occluded(pre,ray,curves[i],geom))
          return true;

      return false;
    }
  };

#else

  /*! Intersector for a single ray with a bezier curve. */
  struct Bezier1iIntersector1
  {
    typedef Bezier1i Primitive;

    static __forceinline void intersect(Ray& ray, const Bezier1i& curve_in, const void* geom)
    {
      /* load bezier curve control points */
      STAT3(normal.trav_prims,1,1,1);
      const Vec3fa v0 = curve_in.p[0];
      const Vec3fa v1 = curve_in.p[1];
      const Vec3fa v2 = curve_in.p[2];
      const Vec3fa v3 = curve_in.p[3];

      /* transform control points into ray space */
      LinearSpace3f ray_space = rcp(frame(ray.dir)); // FIXME: calculate once per ray
      Vec3fa w0 = xfmVector(ray_space,v0-ray.org); w0.w = v0.w;
      Vec3fa w1 = xfmVector(ray_space,v1-ray.org); w1.w = v1.w;
      Vec3fa w2 = xfmVector(ray_space,v2-ray.org); w2.w = v2.w;
      Vec3fa w3 = xfmVector(ray_space,v3-ray.org); w3.w = v3.w;

      /* hit information */
      float ray_u = 0.0f;
      float ray_tfar = ray.tfar;
      bool hit = false;
      
      /* push first curve onto stack */
      BezierCurve3D stack[32];
      new (&stack[0]) BezierCurve3D(w0,w1,w2,w3,0.0f,1.0f,4);
      size_t sptr = 1;

      while (true) 
      {
      pop:
        if (sptr == 0) break;
        BezierCurve3D curve = stack[--sptr];
      
        while (curve.depth)
        {
          BezierCurve3D curve0,curve1;
          curve.subdivide(curve0,curve1);

          BBox3fa bounds0 = curve0.bounds();
          BBox3fa bounds1 = curve1.bounds();
          bool hit0 = intersect_box(bounds0,ray);
          bool hit1 = intersect_box(bounds1,ray);
          
          if (!hit0 && !hit1) goto pop;
          else if (likely(hit0 != hit1)) {
            if (hit0) { curve = curve0; continue; } 
            else      { curve = curve1; continue; }
          } else {
            curve = curve0;
            stack[sptr++] = curve1;
          }
        }

        /* approximative intersection with cone */
        const Vec3fa v = curve.v3-curve.v0;
        const Vec3fa w = -curve.v0;
        const float d0 = w.x*v.x + w.y*v.y;
        const float d1 = v.x*v.x + v.y*v.y;
        const float u = clamp(d0/d1,0.0f,1.0f);
        const Vec3fa p = curve.v0 + u*v;
        const float d2 = p.x*p.x + p.y*p.y; 
        const float r2 = p.w*p.w;
        if (unlikely(d2 > r2)) continue;
        const float t = p.z;
        if (unlikely(t < ray.tnear || t > ray_tfar)) continue;
        ray_u = curve.t0+u*(curve.t1-curve.t0);
        ray_tfar = t;
        hit = true;
      }

      /* compute final hit data */
      if (likely(hit)) 
      {
        BezierCurve3D curve(v0,v1,v2,v3,0.0f,1.0f,0);
        Vec3fa P,T; curve.eval(ray.u,P,T);
        ray.u = ray_u;
        ray.v = 1.0f;
        ray.tfar = ray_tfar;
        ray.Ng = T;
        ray.geomID = curve_in.geomID;
        ray.primID = curve_in.primID;
      }
    }

    static __forceinline void intersect(Ray& ray, const Bezier1i* curves, size_t num, void* geom)
    {
      for (size_t i=0; i<num; i++)
        intersect(ray,curves[i],geom);
    }

    static __forceinline bool occluded(Ray& ray, const Bezier1i& curve_in, const void* geom) {
      return false;
    }

    static __forceinline bool occluded(Ray& ray, const Bezier1i* curves, size_t num, void* geom) 
    {
      for (size_t i=0; i<num; i++) 
        if (occluded(ray,curves[i],geom))
          return true;

      return false;
    }
  };
#endif
}
