//Star Trek Voyager: Elite Force (Holomatch) renderer-side support for the q3 plugin.
//This is included by clq3_ui.c (only when HOLOMATCH is defined), after q3refEntity_t is defined.
//
//Elite Force adds extra refEntity types which its renderer draws as procedural geometry. The engine's renderer knows nothing
//about them, so we build the geometry on the cpu and submit it as polygons (much like VQ3_AddPolys does).
//The geometry follows the descriptions of the types as implemented by ioEF/lilium-voyager (GPLv2).
//Known limitations:
// *the camera-facing types are built when they're added, so they use the previous frame's view origin.
// *shaders using 'depthfunc disable' and the force-entity-alpha behaviour need support from the core shader code.

//Elite Force's refEntity types (a different enum to q3's)
enum
{
	EFRT_MODEL,
	EFRT_SPRITE,
	EFRT_ORIENTEDSPRITE,
	EFRT_ALPHAVERTPOLY,
	EFRT_BEAM,
	EFRT_RAIL_CORE,
	EFRT_RAIL_RINGS,
	EFRT_LIGHTNING,
	EFRT_PORTALSURFACE,
	EFRT_LINE,
	EFRT_ORIENTEDLINE,
	EFRT_LINE2,
	EFRT_BEZIER,
	EFRT_CYLINDER,
	EFRT_ELECTRICITY
};
#define EFRF_FULLBRIGHT			0x0010
#define EFRF_FORCE_ENT_ALPHA	0x0800

static vec3_t vq3_vieworg;	//the previous frame's view origin.
static vec3_t vq3_viewaxis[3];

static void EFV_Normalize(vec3_t v)
{
	float l = sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
	if (l > 0)
	{
		l = 1/l;
		v[0]*=l; v[1]*=l; v[2]*=l;
	}
}
static void EFV_Cross(const vec3_t a, const vec3_t b, vec3_t out)
{
	out[0] = a[1]*b[2] - a[2]*b[1];
	out[1] = a[2]*b[0] - a[0]*b[2];
	out[2] = a[0]*b[1] - a[1]*b[0];
}
static void EFV_RotateAround(vec3_t out, const vec3_t axis/*unit*/, const vec3_t in, float degrees)
{	//rodrigues rotation
	float r = degrees*(M_PI/180), s = sin(r), c = cos(r);
	float d = axis[0]*in[0]+axis[1]*in[1]+axis[2]*in[2];
	vec3_t x;
	EFV_Cross(axis, in, x);
	out[0] = in[0]*c + x[0]*s + axis[0]*d*(1-c);
	out[1] = in[1]*c + x[1]*s + axis[1]*d*(1-c);
	out[2] = in[2]*c + x[2]*s + axis[2]*d*(1-c);
}
static void EFV_Perpendicular(vec3_t dst, const vec3_t src)
{	//any unit vector perpendicular to src (which is expected to be unit-length).
	int i, pos = 0;
	float minelem = 1, a;
	vec3_t tempvec = {0,0,0};
	for (i = 0; i < 3; i++)
	{
		a = src[i]<0?-src[i]:src[i];
		if (a < minelem)
		{
			pos = i;
			minelem = a;
		}
	}
	tempvec[pos] = 1;
	a = tempvec[0]*src[0]+tempvec[1]*src[1]+tempvec[2]*src[2];
	for (i = 0; i < 3; i++)
		dst[i] = tempvec[i] - a*src[i];
	EFV_Normalize(dst);
}
static float EFV_Crandom(void)
{
	return 2.0f * ((rand() & 0x7fff) / (float)0x7fff - 0.5f);
}

//a mesh being built.
typedef struct
{
	vecV_t *xyz;
	vec2_t *st;
	vec4_t *rgba;
	index_t *idx;
	index_t bias;
	int v, i;
} efmesh_t;
static void EFV_Begin(efmesh_t *m, shader_t *s, int numverts, int numindexes)
{
	m->v = m->i = 0;
	m->bias = scenefuncs->AddPolydata(s, BEF_NODLIGHT|BEF_NOSHADOWS, numverts, numindexes, &m->xyz, &m->st, &m->rgba, &m->idx);
}
static void EFV_Vert(efmesh_t *m, const vec3_t org, float s, float t, const qbyte *rgba)
{
	VectorCopy(org, m->xyz[m->v]);
	m->st[m->v][0] = s;
	m->st[m->v][1] = t;
	m->rgba[m->v][0] = rgba[0]*(1/255.0f);
	m->rgba[m->v][1] = rgba[1]*(1/255.0f);
	m->rgba[m->v][2] = rgba[2]*(1/255.0f);
	m->rgba[m->v][3] = rgba[3]*(1/255.0f);
	m->v++;
}
static void EFV_Tri(efmesh_t *m, int a, int b, int c, int base)
{
	m->idx[m->i++] = m->bias + base + a;
	m->idx[m->i++] = m->bias + base + b;
	m->idx[m->i++] = m->bias + base + c;
}

//one rectangle of a line. 'corners' optionally carries the previous end-corners (and is updated with the new ones) so that segments join up.
static void EFV_Line(efmesh_t *m, const vec3_t start, const vec3_t end, const vec3_t left, vec3_t *corners, float starttex, float endtex, const qbyte *rgba)
{
	vec3_t c0, c1, c2, c3;
	int base = m->v;
	if (corners)
	{
		VectorCopy(corners[0], c0);
		VectorCopy(corners[1], c1);
	}
	else
	{
		VectorAdd(start, left, c0);
		VectorSubtract(start, left, c1);
	}
	VectorSubtract(end, left, c2);
	VectorAdd(end, left, c3);
	if (corners)
	{
		VectorCopy(c3, corners[0]);
		VectorCopy(c2, corners[1]);
	}
	EFV_Vert(m, c0, 0, starttex, rgba);
	EFV_Vert(m, c1, 1, starttex, rgba);
	EFV_Vert(m, c2, 1, endtex, rgba);
	EFV_Vert(m, c3, 0, endtex, rgba);
	EFV_Tri(m, 0, 1, 3, base);
	EFV_Tri(m, 3, 1, 2, base);
}
static void EFV_LineNormal(const vec3_t v1, const vec3_t v2, float scale, vec3_t result)
{
	EFV_Cross(v1, v2, result);
	EFV_Normalize(result);
	VectorScale(result, scale/2, result);
}
static void EFV_Quad(efmesh_t *m, const vec3_t origin, const vec3_t l, const vec3_t u, const qbyte *rgba)
{
	vec3_t p;
	int base = m->v;
	VectorAdd(origin, l, p);	VectorAdd(p, u, p);			EFV_Vert(m, p, 0, 0, rgba);
	VectorSubtract(origin, l, p);	VectorAdd(p, u, p);		EFV_Vert(m, p, 1, 0, rgba);
	VectorSubtract(origin, l, p);	VectorSubtract(p, u, p);	EFV_Vert(m, p, 1, 1, rgba);
	VectorAdd(origin, l, p);	VectorSubtract(p, u, p);	EFV_Vert(m, p, 0, 1, rgba);
	EFV_Tri(m, 0, 1, 3, base);
	EFV_Tri(m, 3, 1, 2, base);
}

#define EFBEZIER_SEGMENTS 32
#define EFELEC_SEGMENTS 16
#define EFCYL_MAXPLANES 64

//returns true if we handled it (ie: it was one of the new primitive types).
static qboolean VQ3_AddEntityEF(const q3refEntity_t *q3)
{
	efmesh_t m;
	shader_t *shader;
	vec3_t start, end, dir, s2o, left;
	const qbyte *rgba = q3->shaderRGBA;
	int i;

	switch((int)q3->reType)
	{
	default:
		return false;
	case EFRT_ORIENTEDSPRITE:
	case EFRT_LINE:
	case EFRT_ORIENTEDLINE:
	case EFRT_LINE2:
	case EFRT_BEZIER:
	case EFRT_CYLINDER:
	case EFRT_ELECTRICITY:
		break;
	}

	shader = drawfuncs->ShaderFromId(q3->customShader);
	VectorCopy(q3->origin, start);
	VectorCopy(q3->oldorigin, end);
	VectorSubtract(end, start, dir);
	VectorSubtract(vq3_vieworg, start, s2o);

	switch((int)q3->reType)
	{
	case EFRT_ORIENTEDSPRITE:
		{
			vec3_t l, u;
			float radius = q3->data.sprite.radius, rotation = q3->data.sprite.rotation;
			if (rotation == 0)
			{
				VectorScale(q3->axis[1], radius, l);
				VectorScale(q3->axis[2], radius, u);
			}
			else
			{
				float ang = M_PI * rotation / 180, sn = sin(ang), cs = cos(ang);
				VectorScale(q3->axis[1], cs*radius, l);
				VectorMA(l, -sn*radius, q3->axis[2], l);
				VectorScale(q3->axis[2], cs*radius, u);
				VectorMA(u, sn*radius, q3->axis[1], u);
			}
			EFV_Begin(&m, shader, 4, 6);
			EFV_Quad(&m, start, l, u, rgba);
		}
		break;
	case EFRT_LINE:
		EFV_LineNormal(s2o, dir, q3->data.line.width, left);
		EFV_Begin(&m, shader, 4, 6);
		EFV_Line(&m, start, end, left, NULL, 0, q3->data.line.stscale, rgba);
		break;
	case EFRT_ORIENTEDLINE:
		VectorCopy(q3->axis[1], left);
		EFV_Normalize(left);
		VectorScale(left, q3->data.line.width/2, left);
		EFV_Begin(&m, shader, 4, 6);
		EFV_Line(&m, start, end, left, NULL, 0, 1, rgba);
		break;
	case EFRT_LINE2:	//a tapered line
		{
			vec3_t startleft, endleft, p[6];
			static const float tc[6][2] = {{0,0},{0.5,0},{1,0},{1,1},{0.5,1},{0,1}};
			EFV_Cross(s2o, dir, startleft);
			EFV_Normalize(startleft);
			VectorScale(startleft, q3->data.line.width2/2, endleft);
			VectorScale(startleft, q3->data.line.width/2, startleft);
			VectorAdd(start, startleft, p[0]);
			VectorCopy(start, p[1]);
			VectorSubtract(start, startleft, p[2]);
			VectorSubtract(end, endleft, p[3]);
			VectorCopy(end, p[4]);
			VectorAdd(end, endleft, p[5]);
			EFV_Begin(&m, shader, 6, 12);
			for (i = 0; i < 6; i++)
				EFV_Vert(&m, p[i], tc[i][0], tc[i][1], rgba);
			EFV_Tri(&m, 0, 1, 5, 0);
			EFV_Tri(&m, 5, 4, 1, 0);
			EFV_Tri(&m, 1, 4, 3, 0);
			EFV_Tri(&m, 3, 2, 1, 0);
		}
		break;
	case EFRT_BEZIER:
		{	//cubic bezier, drawn as a strip of camera-facing line segments.
			double t = 0, omt;
			float starttc, endtc;
			vec3_t segstart, segend, prevend[2];
			int idx, segs;
			EFV_Begin(&m, shader, 4*(EFBEZIER_SEGMENTS+1), 6*(EFBEZIER_SEGMENTS+1));
			VectorCopy(start, segstart);
			VectorCopy(segstart, prevend[0]);
			VectorCopy(segstart, prevend[1]);
			for (segs = 0; segs < EFBEZIER_SEGMENTS; segs++)
			{
				starttc = t/2.0;
				t = (segs+1)*(1.0/EFBEZIER_SEGMENTS);
				endtc = t/2.0;
				omt = 1.0 - t;
				for (idx = 0; idx < 3; idx++)
					segend[idx] = start[idx]*omt*omt*omt + 3*q3->data.bezier.control1[idx]*t*omt*omt + 3*q3->data.bezier.control2[idx]*t*t*omt + end[idx]*t*t*t;
				VectorSubtract(segend, segstart, dir);
				VectorSubtract(vq3_vieworg, segstart, s2o);
				EFV_LineNormal(s2o, dir, q3->data.bezier.width, left);
				EFV_Line(&m, segstart, segend, left, prevend, starttc, endtc, rgba);
				VectorCopy(segend, segstart);
			}
			//we asked for slightly more than we used. fill the remainder with degenerate triangles, so we don't leave garbage behind.
			for (idx = 0; m.i < 6*(EFBEZIER_SEGMENTS+1); idx++)
				m.idx[m.i++] = m.bias;
			for (; m.v < 4*(EFBEZIER_SEGMENTS+1); )
				EFV_Vert(&m, start, 0, 0, rgba);
		}
		break;
	case EFRT_ELECTRICITY:
		{	//a jittery multi-segment line
			vec3_t segstart, segend, prevend[2], start2end;
			float veclen, width = q3->data.electricity.width, deviation = q3->data.electricity.deviation;
			VectorSubtract(end, start, start2end);
			veclen = sqrt(start2end[0]*start2end[0]+start2end[1]*start2end[1]+start2end[2]*start2end[2]);
			VectorCopy(start, segend);
			VectorCopy(start, prevend[0]);
			VectorCopy(start, prevend[1]);
			EFV_Begin(&m, shader, 4*EFELEC_SEGMENTS, 6*EFELEC_SEGMENTS);
			for (i = 0; i < EFELEC_SEGMENTS; i++)
			{
				VectorCopy(segend, segstart);
				if (i > EFELEC_SEGMENTS-2)
					VectorCopy(end, segend);
				else
				{
					if (i > EFELEC_SEGMENTS-3)
					{
						VectorAdd(segstart, end, segend);
						VectorScale(segend, 0.5f, segend);
					}
					segend[0] += (start2end[0] + EFV_Crandom()*deviation*veclen) * (1.0/EFELEC_SEGMENTS);
					segend[1] += (start2end[1] + EFV_Crandom()*deviation*veclen) * (1.0/EFELEC_SEGMENTS);
					segend[2] += (start2end[2] + EFV_Crandom()*deviation*veclen/2.0) * (1.0/EFELEC_SEGMENTS);
				}
				VectorSubtract(segend, segstart, dir);
				VectorSubtract(vq3_vieworg, segstart, s2o);
				EFV_LineNormal(s2o, dir, width, left);
				if (!i)
				{
					VectorAdd(segstart, left, prevend[0]);
					VectorSubtract(segstart, left, prevend[1]);
				}
				EFV_Line(&m, segstart, segend, left, prevend, 0, q3->data.electricity.stscale, rgba);
			}
		}
		break;
	case EFRT_CYLINDER:
		{	//actually a frustum.
			int planes, index;
			vec3_t bottom, top, faxis, zerodeg_bottom, zerodeg_top, bottomcorner, topcorner, b2o;
			float anglestep, tcstep = 0, width = q3->data.cylinder.width, width2 = q3->data.cylinder.width2, height = q3->data.cylinder.height;
			float maxradius = (width > width2)?width:width2, distance, d2;

			VectorCopy(q3->axis[0], faxis);
			VectorCopy(start, bottom);
			VectorMA(bottom, height, faxis, top);
			//cheap level-of-detail from the distance to the nearer end.
			VectorSubtract(vq3_vieworg, bottom, b2o);
			distance = sqrt(b2o[0]*b2o[0]+b2o[1]*b2o[1]+b2o[2]*b2o[2]);
			VectorSubtract(vq3_vieworg, top, b2o);
			d2 = sqrt(b2o[0]*b2o[0]+b2o[1]*b2o[1]+b2o[2]*b2o[2]);
			if (d2 < distance)
				distance = d2;
			if (distance < 1)
				distance = 1;
			planes = 4.0f * maxradius/distance * EFCYL_MAXPLANES;
			if (planes < 8)
				planes = 8;
			else if (planes > EFCYL_MAXPLANES)
				planes = EFCYL_MAXPLANES;

			EFV_Perpendicular(zerodeg_bottom, faxis);
			VectorScale(zerodeg_bottom, width2/2.0f, zerodeg_top);
			VectorScale(zerodeg_bottom, width/2.0f, zerodeg_bottom);
			anglestep = 360.0f / planes;
			if (q3->data.cylinder.wrap)
				tcstep = q3->data.cylinder.stscale / planes;

			EFV_Begin(&m, shader, 4*planes, 6*planes);
			for (index = 1; index <= planes; index++)
			{
				int base = m.v;
				vec3_t prevbottom, prevtop;
				float s0, s1;
				const qbyte col[4] = {rgba[0], rgba[1], rgba[2], 0xff};
				if (index <= 1)
				{
					VectorAdd(bottom, zerodeg_bottom, prevbottom);
					VectorAdd(top, zerodeg_top, prevtop);
				}
				else
				{
					VectorCopy(bottomcorner, prevbottom);
					VectorCopy(topcorner, prevtop);
				}
				if (index >= planes)
				{
					VectorAdd(bottom, zerodeg_bottom, bottomcorner);
					VectorAdd(top, zerodeg_top, topcorner);
				}
				else
				{
					EFV_RotateAround(bottomcorner, faxis, zerodeg_bottom, anglestep*index);
					VectorAdd(bottom, bottomcorner, bottomcorner);
					EFV_RotateAround(topcorner, faxis, zerodeg_top, anglestep*index);
					VectorAdd(top, topcorner, topcorner);
				}
				if (q3->data.cylinder.wrap)
				{
					s0 = tcstep*(index-1);
					s1 = tcstep*index;
				}
				else
				{
					s0 = 0;
					s1 = q3->data.cylinder.stscale;
				}
				EFV_Vert(&m, prevbottom, s0, 0, col);
				EFV_Vert(&m, prevtop, s0, 1, col);
				EFV_Vert(&m, bottomcorner, s1, 0, col);
				EFV_Vert(&m, topcorner, s1, 1, col);
				EFV_Tri(&m, 0, 2, 3, base);
				EFV_Tri(&m, 0, 3, 1, base);
			}
		}
		break;
	}
	return true;
}

static int VQ3_EFTypeToEngine(int eftype)
{	//converts to the engine's (q3-compatible) entity types
	switch(eftype)
	{
	case EFRT_SPRITE:
	case EFRT_ALPHAVERTPOLY:	//ef's renderer draws this exactly like a regular sprite.
		return RT_SPRITE;
	case EFRT_BEAM:
		return RT_BEAM;
	case EFRT_RAIL_CORE:
		return RT_RAIL_CORE;
	case EFRT_RAIL_RINGS:
		return RT_RAIL_RINGS;
	case EFRT_LIGHTNING:
		return RT_LIGHTNING;
	case EFRT_PORTALSURFACE:
		return RT_PORTALSURFACE;
	default:
		return RT_MODEL;
	}
}
