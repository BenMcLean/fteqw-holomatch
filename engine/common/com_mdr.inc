//Raven 'MDR' (RDM5) skeletal model loader, as used by Star Trek Voyager: Elite Force.
//Included from com_mesh.c (when HOLOMATCH is defined), directly after the md3 loader, so it can use its static helpers.
//
//MDR has absolute (model-space) bone matrices per frame, with each vertex weighted against up to several bones, each with its own offset.
//The engine's skeletal model support assumes a single bind position per vertex (which isn't something that MDR guarentees), so rather than
//skinning at runtime, we bake each frame into plain vertex-animation data (as for md3). These models are small enough that this is cheap.
//Tags (which are just bones) are baked out as md3-style tags too.
//The format description is based on ioquake3/ioEF's loader (GPLv2).

#define MDR_VERSION	2
#define MDR_QPATH 64	//fixed by the file format (the engine's MAX_QPATH is larger)

typedef struct
{
	int boneIndex;
	float boneWeight;
	vec3_t offset;
} mdrWeight_t;
typedef struct
{
	vec3_t normal;
	vec2_t texCoords;
	int numWeights;
	mdrWeight_t weights[1];	//variable sized
} mdrVertex_t;
typedef struct
{
	int ident;
	char name[MDR_QPATH];
	char shader[MDR_QPATH];
	int shaderIndex;
	int ofsHeader;
	int numVerts;
	int ofsVerts;
	int numTriangles;
	int ofsTriangles;
	int numBoneReferences;
	int ofsBoneReferences;
	int ofsEnd;
} mdrSurface_t;
typedef struct
{
	int numSurfaces;
	int ofsSurfaces;
	int ofsEnd;
} mdrLOD_t;
typedef struct
{
	int boneIndex;
	char name[32];
} mdrTag_t;
typedef struct
{
	int ident;
	int version;
	char name[MDR_QPATH];
	int numFrames;
	int numBones;
	int ofsFrames;	//negative if the bones are compressed.
	int numLODs;
	int ofsLODs;
	int numTags;
	int ofsTags;
	int ofsEnd;
} mdrHeader_t;

//the layout of the per-frame data, which varies depending on whether its compressed.
// uncompressed: vec3 mins, vec3 maxs, vec3 localorigin, float radius, char name[16], float bones[numBones][12]
// compressed:   vec3 mins, vec3 maxs, vec3 localorigin, float radius,                 ushort bones[numBones][12]
#define MDR_FRAMEHEAD_BYTES (3*3*4+4)
#define MDR_COMPBONE_BYTES 24

static void MDR_UnCompressBone(float mat[12], const qbyte *comp)
{
	int i;
	unsigned short v[12];
	for (i = 0; i < 12; i++)
		v[i] = comp[i*2] | (comp[i*2+1]<<8);	//little endian shorts.
	//position (16bit, with 1/64th unit precision)
	mat[3]  = ((int)v[0]-32768) * (1.0f/64);
	mat[7]  = ((int)v[1]-32768) * (1.0f/64);
	mat[11] = ((int)v[2]-32768) * (1.0f/64);
	//the 3x3 matrix.
#define MDR_VECTSCALE (1.0f/(float)((1<<15)-2))
	mat[0]  = ((int)v[3]-32768) * MDR_VECTSCALE;
	mat[1]  = ((int)v[4]-32768) * MDR_VECTSCALE;
	mat[2]  = ((int)v[5]-32768) * MDR_VECTSCALE;
	mat[4]  = ((int)v[6]-32768) * MDR_VECTSCALE;
	mat[5]  = ((int)v[7]-32768) * MDR_VECTSCALE;
	mat[6]  = ((int)v[8]-32768) * MDR_VECTSCALE;
	mat[8]  = ((int)v[9]-32768) * MDR_VECTSCALE;
	mat[9]  = ((int)v[10]-32768) * MDR_VECTSCALE;
	mat[10] = ((int)v[11]-32768) * MDR_VECTSCALE;
}

static galiasinfo_t *Mod_LoadMDRLod(model_t *mod, int *surfcount, const mdrHeader_t *header, size_t fsize, const mdrLOD_t *lod, const float *bones, unsigned int numframes, unsigned int numbones, frameinfo_t *framegroups, unsigned int numgroups)
{
	galiasinfo_t *first = NULL, **surflink = &first;
	const mdrSurface_t *surf;
	unsigned int s, i, j, k, f, d;
	vec3_t min, max;
	int externalskins = 0;
	const qbyte *base = (const qbyte*)header;

#ifndef SERVERONLY
	externalskins = Mod_CountSkinFiles(mod);
#endif

	ClearBounds(min, max);

	surf = (const mdrSurface_t*)((const qbyte*)lod + LittleLong(lod->ofsSurfaces));
	for (s = 0; s < (unsigned int)LittleLong(lod->numSurfaces); s++, *surfcount+=1)
	{
		galiasinfo_t *galias;
		galiasanimation_t *group;
		galiaspose_t *pose;
		vecV_t *verts;
		vec3_t *normals, *svector, *tvector;
		vec2_t *st_array;
		index_t *indexes;
		unsigned int numverts = LittleLong(surf->numVerts);
		unsigned int numtris = LittleLong(surf->numTriangles);
		const mdrVertex_t *v;
		const int *tri;
		size_t size;
		unsigned int numg = framegroups?numgroups:numframes;

		if ((const qbyte*)surf < base || (const qbyte*)surf + sizeof(*surf) > base+fsize)
			break;

		size = sizeof(galiasinfo_t) + sizeof(galiasanimation_t)*numg;
		galias = ZG_Malloc(&mod->memgroup, size);
		Mod_DefaultMesh(galias, surf->name, *surfcount);
		galias->ofsanimations = group = (galiasanimation_t*)(galias+1);
		galias->numanimations = numg;
		galias->numverts = numverts;
		galias->numindexes = numtris*3;
		galias->shares_verts = *surfcount;

		if (!first)
			first = galias;
		*surflink = galias;
		surflink = &galias->nextsurf;
		galias->nextsurf = NULL;

		//indexes
		indexes = ZG_Malloc(&mod->memgroup, sizeof(*indexes)*galias->numindexes);
		galias->ofs_indexes = indexes;
		tri = (const int*)((const qbyte*)surf + LittleLong(surf->ofsTriangles));
		for (i = 0; i < numtris; i++)
		{
			for (j = 0; j < 3; j++)
			{
				indexes[i*3+j] = LittleLong(tri[i*3+j]);
				if (indexes[i*3+j] >= numverts)
				{
					Con_Printf(CON_WARNING "Warning: mdr surface %s has invalid vertex indexes\n", galias->surfacename);
					indexes[i*3+0] = indexes[i*3+1] = indexes[i*3+2] = 0;
					break;
				}
			}
		}

		//texcoords, and the poses.
#ifndef SERVERONLY
		st_array = ZG_Malloc(&mod->memgroup, sizeof(vec2_t)*numverts);
		galias->ofs_st_array = st_array;
#else
		st_array = NULL;
#endif
		size = sizeof(galiaspose_t) + sizeof(vecV_t)*numverts;
#ifndef SERVERONLY
		size += 3*sizeof(vec3_t)*numverts;
#endif
		size *= numframes;
		pose = (galiaspose_t*)ZG_Malloc(&mod->memgroup, size);
		verts = (vecV_t*)(pose+numframes);
#ifndef SERVERONLY
		normals = (vec3_t*)(verts + numverts*numframes);
		svector = (vec3_t*)(normals + numverts*numframes);
		tvector = (vec3_t*)(svector + numverts*numframes);
#else
		normals = svector = tvector = NULL;
#endif

		//walk the vertices, once per frame (they're variable-length, so we need to walk them every time).
		for (f = 0; f < numframes; f++)
		{
			const float *fbones = bones + f*numbones*12;
			v = (const mdrVertex_t*)((const qbyte*)surf + LittleLong(surf->ofsVerts));
			for (j = 0; j < numverts; j++)
			{
				vec3_t org = {0,0,0}, nrm = {0,0,0};
				int nw = LittleLong(v->numWeights);
				const mdrWeight_t *w = v->weights;
				if (nw < 0 || (const qbyte*)w + nw*sizeof(*w) > base+fsize)
				{
					Con_Printf(CON_WARNING "mdr: broken vertex data in %s\n", mod->name);
					nw = 0;
				}
				for (k = 0; k < nw; k++, w++)
				{
					int bi = LittleLong(w->boneIndex);
					float bw = LittleFloat(w->boneWeight);
					vec3_t off, nv;
					const float *m;
					if (bi < 0 || bi >= (int)numbones)
						continue;
					m = fbones + bi*12;
					off[0] = LittleFloat(w->offset[0]); off[1] = LittleFloat(w->offset[1]); off[2] = LittleFloat(w->offset[2]);
					org[0] += bw * (m[0]*off[0] + m[1]*off[1] + m[2]*off[2]  + m[3]);
					org[1] += bw * (m[4]*off[0] + m[5]*off[1] + m[6]*off[2]  + m[7]);
					org[2] += bw * (m[8]*off[0] + m[9]*off[1] + m[10]*off[2] + m[11]);
#ifndef SERVERONLY
					nv[0] = LittleFloat(v->normal[0]); nv[1] = LittleFloat(v->normal[1]); nv[2] = LittleFloat(v->normal[2]);
					nrm[0] += bw * (m[0]*nv[0] + m[1]*nv[1] + m[2]*nv[2]);
					nrm[1] += bw * (m[4]*nv[0] + m[5]*nv[1] + m[6]*nv[2]);
					nrm[2] += bw * (m[8]*nv[0] + m[9]*nv[1] + m[10]*nv[2]);
#endif
				}
				for (d = 0; d < 3; d++)
				{
					verts[j][d] = org[d];
					if (org[d] < min[d])
						min[d] = org[d];
					if (org[d] > max[d])
						max[d] = org[d];
#ifndef SERVERONLY
					normals[j][d] = nrm[d];
#endif
				}
#ifndef SERVERONLY
				if (!f)
				{
					st_array[j][0] = LittleFloat(v->texCoords[0]);
					st_array[j][1] = LittleFloat(v->texCoords[1]);
				}
#endif
				v = (const mdrVertex_t*)&v->weights[nw];	//(nw was clamped, but if it was bad we've already warned)
			}

			pose->scale[0] = pose->scale[1] = pose->scale[2] = 1;
			pose->scale_origin[0] = pose->scale_origin[1] = pose->scale_origin[2] = 0;
			pose->ofsverts = verts;
			verts += numverts;
#ifndef SERVERONLY
			pose->ofsnormals = normals;
			normals += numverts;
			pose->ofssvector = svector;
			svector += numverts;
			pose->ofstvector = tvector;
			tvector += numverts;
#endif
			pose++;
		}
		pose -= numframes;

		//animation groups
		if (framegroups)
		{
			for (i = 0; i < numgroups; i++, group++)
			{
				Q_snprintfz(group->name, sizeof(group->name), "%s", framegroups[i].name);
				if (framegroups[i].posesarray)
				{
					unsigned int p, targpose;
					group->poseofs = ZG_Malloc(&mod->memgroup, sizeof(*group->poseofs) * framegroups[i].posecount);
					group->numposes = 0;
					for (p = 0; p < framegroups[i].posecount; p++)
					{
						targpose = framegroups[i].poses[p];
						if (targpose < numframes)
							group->poseofs[group->numposes++] = pose[targpose];
					}
				}
				else
				{
					int first = framegroups[i].firstpose, count = framegroups[i].posecount;
					if (first >= (int)numframes)
						first = numframes-1;
					if (first < 0)
						first = 0;
					if (count > (int)numframes-first)
						count = numframes-first;
					if (count < 0)
						count = 0;
					group->poseofs = pose + first;
					group->numposes = count;
				}
				group->rate = framegroups[i].fps;
				group->loop = framegroups[i].loop;
				group->events = framegroups[i].events;
				group->action = framegroups[i].action;
				group->actionweight = framegroups[i].actionweight;
			}
		}
		else
		{
			for (i = 0; i < numframes; i++, group++)
			{
				Q_snprintfz(group->name, sizeof(group->name), "frame%i", i);
				group->numposes = 1;
				group->rate = 1;
				group->poseofs = pose + i;
				group->loop = false;
				group->events = NULL;
				group->action = -1;
				group->actionweight = 0;
			}
		}

#ifndef SERVERONLY
		{	//skins. mdr only has the one shader, but there may be .skin files too.
			galiasskin_t *skin;
			skinframe_t *frames;
			int numskins = externalskins;
			if (numskins < 1)
				numskins = 1;
			skin = ZG_Malloc(&mod->memgroup, numskins*(sizeof(galiasskin_t)+sizeof(skinframe_t)));
			galias->ofsskins = skin;
			frames = (skinframe_t*)(skin + numskins);
			for (i = 0; i < (unsigned int)numskins; i++, skin++)
			{
				skin->numframes = 1;
				skin->frame = &frames[i];
				skin->skinwidth = 0;
				skin->skinheight = 0;
				skin->skinspeed = 0;
				if (!i)
				{
					Q_strncpyz(frames[i].shadername, surf->shader, sizeof(frames[i].shadername));
					Q_strncpyz(skin->name, surf->shader, sizeof(skin->name));
				}
			}
			galias->numskins = numskins;
		}
#endif

		Mod_CompileTriangleNeighbours(mod, galias);
		Mod_BuildTextureVectors(galias);

		surf = (const mdrSurface_t*)((const qbyte*)surf + LittleLong(surf->ofsEnd));
	}

	AddPointToBounds(min, mod->mins, mod->maxs);
	AddPointToBounds(max, mod->mins, mod->maxs);
	return first;
}

static qboolean QDECL Mod_LoadMDR(model_t *mod, void *buffer, size_t fsize)
{
	const mdrHeader_t *header = buffer;
	unsigned int numframes, numbones, i, j, numlods, lod;
	float *bones;
	const qbyte *fp;
	qboolean compressed;
	size_t framesize, framehead;
	frameinfo_t *framegroups;
	unsigned int numgroups = 0;
	galiasinfo_t *root, *last = NULL, *sub, *prevlod = NULL;
	int surfcount = 0;
	int ofsframes;

	if (fsize < sizeof(*header) || LittleLong(header->version) != MDR_VERSION)
	{
		Con_Printf(CON_ERROR "Mod_LoadMDR: %s has wrong version\n", mod->name);
		return false;
	}
	if ((size_t)LittleLong(header->ofsEnd) > fsize)
	{
		Con_Printf(CON_ERROR "Mod_LoadMDR: %s is truncated\n", mod->name);
		return false;
	}
	numframes = LittleLong(header->numFrames);
	numbones = LittleLong(header->numBones);
	numlods = LittleLong(header->numLODs);
	ofsframes = LittleLong(header->ofsFrames);
	if (!numframes || numframes > 65536 || numbones > 1024 || !numlods)
	{
		Con_Printf(CON_ERROR "Mod_LoadMDR: %s has no frames/lods\n", mod->name);
		return false;
	}

	compressed = ofsframes < 0;
	if (compressed)
		ofsframes = -ofsframes;
	framehead = compressed?MDR_FRAMEHEAD_BYTES:(MDR_FRAMEHEAD_BYTES+16);
	framesize = framehead + numbones*(compressed?MDR_COMPBONE_BYTES:sizeof(float)*12);
	if ((size_t)ofsframes + framesize*numframes > fsize)
	{
		Con_Printf(CON_ERROR "Mod_LoadMDR: %s has broken frames\n", mod->name);
		return false;
	}

	//decode all the bones, so we have simple floats to work with.
	bones = BZ_Malloc(sizeof(float)*12*numbones*numframes);
	fp = (const qbyte*)buffer + ofsframes;
	for (i = 0; i < numframes; i++, fp += framesize)
	{
		const qbyte *b = fp + framehead;
		for (j = 0; j < numbones; j++)
		{
			float *m = bones + (i*numbones+j)*12;
			if (compressed)
				MDR_UnCompressBone(m, b + j*MDR_COMPBONE_BYTES);
			else
			{
				int k;
				for (k = 0; k < 12; k++)
					m[k] = LittleFloat(((const float*)b)[j*12+k]);
			}
		}
	}

	ClearBounds(mod->mins, mod->maxs);
	framegroups = ParseFrameInfo(mod, &numgroups);

	//each lod is a completely separate set of surfaces, sharing the bones and frames.
	root = NULL;
	for (lod = 0; lod < numlods; lod++)
	{
		const mdrLOD_t *l = (const mdrLOD_t*)((const qbyte*)buffer + LittleLong(header->ofsLODs));
		for (i = 0; i < lod; i++)
			l = (const mdrLOD_t*)((const qbyte*)l + LittleLong(l->ofsEnd));
		sub = Mod_LoadMDRLod(mod, &surfcount, header, fsize, l, bones, numframes, numbones, framegroups, numgroups);
		if (!sub)
			break;
		if (!root)
			root = sub;
		else
		{	//the previous lod stops being used when this one starts.
			for (last = prevlod; last; last = last->nextsurf)
			{
				last->maxdist = lod;
				if (!last->nextsurf)
				{
					last->nextsurf = sub;
					break;
				}
			}
			for (last = sub; last; last = last->nextsurf)
				last->mindist = lod;
			mod->maxlod = lod+1;
		}
		prevlod = sub;
	}

	if (!root)
	{
		root = ZG_Malloc(&mod->memgroup, sizeof(galiasinfo_t));
		Mod_DefaultMesh(root, mod->name, 0);
	}

	//tags are just bones.
	root->numtagframes = numframes;
	root->numtags = LittleLong(header->numTags);
	if (root->numtags < 0 || (const qbyte*)buffer+LittleLong(header->ofsTags)+root->numtags*sizeof(mdrTag_t) > (const qbyte*)buffer+fsize)
		root->numtags = 0;
	root->ofstags = ZG_Malloc(&mod->memgroup, sizeof(md3tag_t)*(root->numtags*numframes+1));
	{
		const mdrTag_t *tags = (const mdrTag_t*)((const qbyte*)buffer + LittleLong(header->ofsTags));
		for (i = 0; i < numframes; i++)
		{
			for (j = 0; j < (unsigned int)root->numtags; j++)
			{
				md3tag_t *dst = &root->ofstags[i*root->numtags+j];
				unsigned int bi = LittleLong(tags[j].boneIndex);
				const float *m;
				int a, b;
				Q_strncpyz(dst->name, tags[j].name, sizeof(dst->name));
				if (bi >= numbones)
				{
					memset(dst->ang, 0, sizeof(dst->ang));
					dst->ang[0][0] = dst->ang[1][1] = dst->ang[2][2] = 1;
					VectorClear(dst->org);
					continue;
				}
				m = bones + (i*numbones+bi)*12;
				for (a = 0; a < 3; a++)
					for (b = 0; b < 3; b++)
						dst->ang[a][b] = m[b*4+a];	//axis a is column a of the matrix
				dst->org[0] = m[3];
				dst->org[1] = m[7];
				dst->org[2] = m[11];
			}
		}
	}

	BZ_Free(bones);
	free(framegroups);

	mod->radius = RadiusFromBounds(mod->mins, mod->maxs);
	mod->flags = Mod_ReadFlagsFromMD1(mod->name, 0);

	Mod_ClampModelSize(mod);
	Mod_ParseModelEvents(mod, root->ofsanimations, root->numanimations);

	mod->type = mod_alias;
	mod->numframes = root->numanimations;
	mod->meshinfo = root;
	mod->funcs.NativeTrace = Mod_Trace;
	return true;
}
