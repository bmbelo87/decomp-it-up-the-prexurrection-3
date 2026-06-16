#ifndef VSL_H
#define VSL_H

#include "pumpy.h"

#define VSL_SIG_LEN 15
#define VSL_MAX_MATERIALS 99
#define VSL_MAX_MESHES 512
#define VSL_MAX_ANIMS 500
#define VSL_MAX_VERTICES_PER_MESH 65536
#define VSL_MAX_FACES_PER_MESH 65536
#define VSL_MAX_ANIM_KEYFRAMES 1000
#define VSL_MAX_IFL_CACHE 32
#define VSL_MAX_IFL_FRAMES 256

#define TC_MAX_SUBMESHES 64
#define TC_MAX_VERTICES 65536
#define TC_MAX_VERTICES_PER_MESH 65536
#define TC_MAX_ANIMS 256

#pragma pack(push, 1)
typedef struct {
    int16_t meshIdx[3];
    int16_t animIdx[3];
} VSLFrame;
#pragma pack(pop)

typedef struct {
    float x, y, z;
    float u, v;
} TCVertex;

typedef struct {
    float m[4][4];
} TCAnimKeyframe;

typedef struct {
    int materialIdx;
    int faceCount;
    int vertexStart;
    TCVertex* vertices;
} TCSubMesh;

typedef struct {
    char name[32];
    int materialCount;
    float* ambient;
    float* diffuse;
    float* specular;
    float* emissive;
    float* shininess;
    int hasAnimation;
    char animName1[32];
    char animName2[32];
    int texWidth;
    int texHeight;
    int texFormat;
    GLuint texId;
    bool texLoaded;
    int flags;
    float scaleU;
    float scaleV;
} TCMaterial;

typedef struct {
    char name[32];
    int subMeshCount;
    int* subMeshMaterialIdx;
    int* subMeshFaceCounts;
    TCVertex* vertices;
    int totalVertices;
    int animCount;
    TCAnimKeyframe* anims;
    int faceCount;
    float lineWidth;
} TCMeshGroup;

typedef struct {
    char name[32];
    int meshGroupCount;
    int* meshGroupIndices;
    int meshGroupFaceCount;
} TCSubGroup;

typedef struct {
    int count;
    TCSubGroup* groups;
    int* faceCounts;
    TCVertex* vertices;
    int totalVertices;
    int animCount;
    TCAnimKeyframe* anims;
} TCMeshEntry;

typedef struct {
    int frameCount;
    VSLFrame* frames;
} VSLLayerData;

typedef struct {
    char name[32];
    TCMeshEntry meshEntry;
    int materialIdx;
    int faceCount;
} VSLSubObject;

typedef struct {
    char name[32];
    TCMeshEntry* meshGroups;
    int meshGroupCount;
    int materialCount;
    int groupStart;
    int groupCount;
} VSLMeshTable;

typedef struct {
    char name[32];
    int frameCount;
    char frameNames[VSL_MAX_IFL_FRAMES][32];
} VSLAIFLCache;

typedef struct {
    bool active;
    int materialCount;
    char materialNames[VSL_MAX_MATERIALS][32];
    int frameCount;
    VSLFrame* frames;
    int meshTableCount;
    VSLMeshTable meshTables[VSL_MAX_MESHES];
    int tcCount;
    TCMaterial tcMaterials[500];
    int tcActive[500];
    char tcNames[500][32];
    int iflCacheCount;
    VSLAIFLCache iflCache[VSL_MAX_IFL_CACHE];
} VSLScreen;

extern VSLScreen g_vsl;

bool VSL_IsVSL(const char* datPath);
bool VSL_Load(const char* datPath);
void VSL_Render(int frame);
void VSL_Shutdown(void);
void VSL_AdvanceFrame(float dt);

#endif