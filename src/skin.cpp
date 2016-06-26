#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include "rwbase.h"
#include "rwerror.h"
#include "rwplg.h"
#include "rwpipeline.h"
#include "rwobjects.h"
#include "rwengine.h"
#include "rwplugins.h"
#include "ps2/rwps2.h"
#include "ps2/rwps2plg.h"
#include "d3d/rwxbox.h"
#include "d3d/rwd3d8.h"
#include "d3d/rwd3d9.h"
#include "gl/rwwdgl.h"
#include "gl/rwgl3.h"

#define PLUGIN_ID ID_SKIN

namespace rw {

SkinGlobals skinGlobals = { 0, { nil } };

static void*
createSkin(void *object, int32 offset, int32)
{
	*PLUGINOFFSET(Skin*, object, offset) = nil;
	return object;
}

static void*
destroySkin(void *object, int32 offset, int32)
{
	Skin *skin = *PLUGINOFFSET(Skin*, object, offset);
	if(skin){
		delete[] skin->data;
//		delete[] skin->platformData;
	}
	delete skin;
	return object;
}

static void*
copySkin(void *dst, void *src, int32 offset, int32)
{
	Skin *srcskin = *PLUGINOFFSET(Skin*, src, offset);
	if(srcskin == nil)
		return dst;
	Geometry *geometry = (Geometry*)src;
	assert(geometry->instData == nil);
	assert(((Geometry*)src)->numVertices == ((Geometry*)dst)->numVertices);
	Skin *dstskin = new Skin;
	*PLUGINOFFSET(Skin*, dst, offset) = dstskin;
	dstskin->numBones = srcskin->numBones;
	dstskin->numUsedBones = srcskin->numUsedBones;
	dstskin->numWeights = srcskin->numWeights;

	assert(0 && "can't copy skin yet");
	dstskin->init(srcskin->numBones, srcskin->numUsedBones, geometry->numVertices);
	memcpy(dstskin->usedBones, srcskin->usedBones, srcskin->numUsedBones);
	memcpy(dstskin->inverseMatrices, srcskin->inverseMatrices,
	       srcskin->numBones*64);
	memcpy(dstskin->indices, srcskin->indices, geometry->numVertices*4);
	memcpy(dstskin->weights, srcskin->weights, geometry->numVertices*16);
	return dst;
}

static Stream*
readSkin(Stream *stream, int32 len, void *object, int32 offset, int32)
{
	uint8 header[4];
	Geometry *geometry = (Geometry*)object;

	if(geometry->instData){
		// TODO: function pointers
		if(geometry->instData->platform == PLATFORM_PS2)
			return ps2::readNativeSkin(stream, len, object, offset);
		else if(geometry->instData->platform == PLATFORM_WDGL)
			return wdgl::readNativeSkin(stream, len, object, offset);
		else if(geometry->instData->platform == PLATFORM_XBOX)
			return xbox::readNativeSkin(stream, len, object, offset);
		else{
			assert(0 && "unsupported native skin platform");
			return nil;
		}
	}

	stream->read(header, 4);	// numBones, numUsedBones, numWeights, unused
	Skin *skin = new Skin;
	*PLUGINOFFSET(Skin*, geometry, offset) = skin;

	// numUsedBones and numWeights appear in/after 34003 but not in/before 33002
	// (probably rw::version >= 0x34000)
	bool oldFormat = header[1] == 0;

	// Use numBones for numUsedBones to allocate data, find out the correct value later
	if(oldFormat)
		skin->init(header[0], header[0], geometry->numVertices);
	else
		skin->init(header[0], header[1], geometry->numVertices);
	skin->numWeights = header[2];

	if(!oldFormat)
		stream->read(skin->usedBones, skin->numUsedBones);
	if(skin->indices)
		stream->read(skin->indices, geometry->numVertices*4);
	if(skin->weights)
		stream->read(skin->weights, geometry->numVertices*16);
	for(int32 i = 0; i < skin->numBones; i++){
		if(oldFormat)
			stream->seek(4);	// skip 0xdeaddead
		stream->read(&skin->inverseMatrices[i*16], 64);

		//{
		//float *mat = &skin->inverseMatrices[i*16];
		//printf("[ [ %8.4f, %8.4f, %8.4f, %8.4f ]\n"
		//       "  [ %8.4f, %8.4f, %8.4f, %8.4f ]\n"
		//       "  [ %8.4f, %8.4f, %8.4f, %8.4f ]\n"
		//       "  [ %8.4f, %8.4f, %8.4f, %8.4f ] ]\n",
		//	mat[0], mat[4], mat[8], mat[12],
		//	mat[1], mat[5], mat[9], mat[13],
		//	mat[2], mat[6], mat[10], mat[14],
		//	mat[3], mat[7], mat[11], mat[15]);
		//}
	}

	if(oldFormat){
		skin->findNumWeights(geometry->numVertices);
		skin->findUsedBones(geometry->numVertices);
	}

	// no split skins in GTA
	if(!oldFormat)
		stream->seek(12);
	return stream;
}

static Stream*
writeSkin(Stream *stream, int32 len, void *object, int32 offset, int32)
{
	uint8 header[4];
	Geometry *geometry = (Geometry*)object;

	if(geometry->instData){
		if(geometry->instData->platform == PLATFORM_PS2)
			ps2::writeNativeSkin(stream, len, object, offset);
		else if(geometry->instData->platform == PLATFORM_WDGL)
			wdgl::writeNativeSkin(stream, len, object, offset);
		else if(geometry->instData->platform == PLATFORM_XBOX)
			xbox::writeNativeSkin(stream, len, object, offset);
		else{
			assert(0 && "unsupported native skin platform");
			return nil;
		}
	}

	Skin *skin = *PLUGINOFFSET(Skin*, object, offset);
	// not sure which version introduced the new format
	bool oldFormat = version < 0x34000;
	header[0] = skin->numBones;
	if(oldFormat){
		header[1] = 0;
		header[2] = 0;
	}else{
		header[1] = skin->numUsedBones;
		header[2] = skin->numWeights;
	}
	header[3] = 0;
	stream->write(header, 4);
	if(!oldFormat)
		stream->write(skin->usedBones, skin->numUsedBones);
	stream->write(skin->indices, geometry->numVertices*4);
	stream->write(skin->weights, geometry->numVertices*16);
	for(int32 i = 0; i < skin->numBones; i++){
		if(oldFormat)
			stream->writeU32(0xdeaddead);
		stream->write(&skin->inverseMatrices[i*16], 64);
	}

	// no split skins in GTA
	if(!oldFormat){
		uint32 buffer[3] = { 0, 0, 0};
		stream->write(buffer, 12);
	}
	return stream;
}

static int32
getSizeSkin(void *object, int32 offset, int32)
{
	Geometry *geometry = (Geometry*)object;

	if(geometry->instData){
		if(geometry->instData->platform == PLATFORM_PS2)
			return ps2::getSizeNativeSkin(object, offset);
		if(geometry->instData->platform == PLATFORM_WDGL)
			return wdgl::getSizeNativeSkin(object, offset);
		if(geometry->instData->platform == PLATFORM_XBOX)
			return xbox::getSizeNativeSkin(object, offset);
		if(geometry->instData->platform == PLATFORM_D3D8)
			return -1;
		if(geometry->instData->platform == PLATFORM_D3D9)
			return -1;
		assert(0 && "unsupported native skin platform");
	}

	Skin *skin = *PLUGINOFFSET(Skin*, object, offset);
	if(skin == nil)
		return -1;

	int32 size = 4 + geometry->numVertices*(16+4) +
	             skin->numBones*64;
	// not sure which version introduced the new format
	if(version < 0x34000)
		size += skin->numBones*4;
	else
		size += skin->numUsedBones + 12;
	return size;
}

static void
skinRights(void *object, int32, int32, uint32)
{
	Skin::setPipeline((Atomic*)object, 1);
}

static void*
skinOpen(void*, int32, int32)
{
	skinGlobals.pipelines[PLATFORM_PS2] =
		ps2::makeSkinPipeline();
	skinGlobals.pipelines[PLATFORM_XBOX] =
		xbox::makeSkinPipeline();
	skinGlobals.pipelines[PLATFORM_D3D8] =
		d3d8::makeSkinPipeline();
	skinGlobals.pipelines[PLATFORM_D3D9] =
		d3d9::makeSkinPipeline();
	skinGlobals.pipelines[PLATFORM_WDGL] =
		wdgl::makeSkinPipeline();
	skinGlobals.pipelines[PLATFORM_GL3] =
		gl3::makeSkinPipeline();
}

static void*
skinClose(void*, int32, int32)
{
}

void
registerSkinPlugin(void)
{
	// init dummy pipelines
	ObjPipeline *defpipe = new ObjPipeline(PLATFORM_NULL);
	defpipe->pluginID = ID_SKIN;
	defpipe->pluginData = 1;
	for(uint i = 0; i < nelem(skinGlobals.pipelines); i++)
		skinGlobals.pipelines[i] = defpipe;

	// TODO: call platform specific init functions?
	//       have platform specific open functions?

	Engine::registerPlugin(0, ID_SKIN, skinOpen, skinClose);

	skinGlobals.offset = Geometry::registerPlugin(sizeof(Skin*), ID_SKIN,
	                                              createSkin,
	                                              destroySkin,
	                                              copySkin);
	Geometry::registerPluginStream(ID_SKIN,
	                               readSkin, writeSkin, getSizeSkin);
	Atomic::registerPlugin(0, ID_SKIN, nil, nil, nil);
	Atomic::setStreamRightsCallback(ID_SKIN, skinRights);
}

void
Skin::init(int32 numBones, int32 numUsedBones, int32 numVertices)
{
	this->numBones = numBones;
	this->numUsedBones = numUsedBones;
	uint32 size = this->numUsedBones +
	              this->numBones*64 +
	              numVertices*(16+4) + 0xF;
	this->data = new uint8[size];
	uint8 *p = this->data;

	this->usedBones = nil;
	if(this->numUsedBones){
		this->usedBones = p;
		p += this->numUsedBones;
	}

	p = (uint8*)(((uintptr)p + 0xF) & ~0xF);
	this->inverseMatrices = nil;
	if(this->numBones){
		this->inverseMatrices = (float*)p;
		p += 64*this->numBones;
	}

	this->indices = nil;
	if(numVertices){
		this->indices = p;
		p += 4*numVertices;
	}

	this->weights = nil;
	if(numVertices)
		this->weights = (float*)p;

	this->platformData = nil;
}

void
Skin::findNumWeights(int32 numVertices)
{
	this->numWeights = 1;
	float *w = this->weights;
	while(numVertices--){
		while(w[this->numWeights] != 0.0f){
			this->numWeights++;
			if(this->numWeights == 4)
				return;
		}
		w += 4;
	}
}

void
Skin::findUsedBones(int32 numVertices)
{
	uint8 usedTab[256];
	uint8 *indices = this->indices;
	float *weights = this->weights;
	memset(usedTab, 0, 256);
	while(numVertices--){
		for(int32 i = 0; i < this->numWeights; i++){
			if(weights[i] == 0.0f)
				continue;	// TODO: this could probably be optimized
			if(usedTab[indices[i]] == 0)
				usedTab[indices[i]]++;
		}
		indices += 4;
		weights += 4;
	}
	this->numUsedBones = 0;
	for(int32 i = 0; i < 256; i++)
		if(usedTab[i])
			this->usedBones[this->numUsedBones++] = i;
}

void
Skin::setPipeline(Atomic *a, int32 type)
{
	(void)type;
	a->pipeline = skinGlobals.pipelines[rw::platform];
}

}
