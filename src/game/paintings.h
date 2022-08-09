#ifndef PAINTINGS_H
#define PAINTINGS_H

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "macros.h"
#include "types.h"

/// Use to properly set a GraphNodeGenerated's parameter to point to the right painting.
/// Use this for both bparam1 and bparam2 for painting objects.
#define PAINTING_ID(id, grp) ((id) | ((grp) << 8))

/// The default painting side length.
#define PAINTING_SIZE 614.0f

/// The depth of the area in front of the painting which triggers ripples without warping.
#define PAINTING_WOBBLE_DEPTH 100.0f

/// The depth of the area behind the painting which triggers the warp.
#define PAINTING_WARP_DEPTH PAINTING_SIZE

/// The space around the edges in which Mario is still considered within painting bounds.
#define PAINTING_EDGE_MARGIN (PAINTING_SIZE / 2)

/// This is added to Mario's Y position to make the ripple closer to Mario's center of mass.
#define PAINTING_MARIO_Y_OFFSET 50.0f

// HMC painting group
enum HMCPaintingIDs {
    /* 0*/ PAINTING_ID_HMC_COTMC,
};

// Inside Castle painting group
enum CastlePaintingIDs {
    /* 0*/ PAINTING_ID_CASTLE_BOB,
    /* 1*/ PAINTING_ID_CASTLE_CCM,
    /* 2*/ PAINTING_ID_CASTLE_WF,
    /* 3*/ PAINTING_ID_CASTLE_JRB,
    /* 4*/ PAINTING_ID_CASTLE_LLL,
    /* 5*/ PAINTING_ID_CASTLE_SSL,
    /* 6*/ PAINTING_ID_CASTLE_HMC,
    /* 7*/ PAINTING_ID_CASTLE_DDD,
    /* 8*/ PAINTING_ID_CASTLE_WDW,
    /* 9*/ PAINTING_ID_CASTLE_THI_TINY,
    /*10*/ PAINTING_ID_CASTLE_TTM,
    /*11*/ PAINTING_ID_CASTLE_TTC,
    /*12*/ PAINTING_ID_CASTLE_SL,
    /*13*/ PAINTING_ID_CASTLE_THI_HUGE,
    /*14*/ PAINTING_ID_CASTLE_RR,
};

// TTM painting group
enum TTMPaintingIDs {
    /* 0*/ PAINTING_ID_TTM_SLIDE,
};

// Painting group ids
enum PaintingGroups {
    /* 0*/PAINTING_GROUP_HMC,
    /* 1*/PAINTING_GROUP_INSIDE_CASTLE,
    /* 2*/PAINTING_GROUP_TTM,
    PAINTING_NUM_GROUPS,
    PAINTING_GROUP_NULL = -1,
};

// Painting->state
enum PaintingState {
    PAINTING_IDLE,
    PAINTING_RIPPLE,
    PAINTING_ENTERED,
};

// Painting->rippleTrigger
enum RippleTriggers {
    RIPPLE_TRIGGER_PROXIMITY,
    RIPPLE_TRIGGER_CONTINUOUS,
};

// Painting->lastFlags, Painting->currFlags, Painting->changedFlags
enum PaintingRippleFlags {
    // Not rippling.
    RIPPLE_FLAGS_NONE  = 0x0,
    // Triggers an entry ripple.
    RIPPLE_FLAG_ENTER  = BIT(0), // 0x01
    // Triggers a passive ripple.
    RIPPLE_FLAG_RIPPLE = BIT(1), // 0x02
};

// Painting->textureType
enum PaintingType {
    /// Painting that uses 1 or more images as a texture
    PAINTING_IMAGE,
    /// Painting that has one texture used for an environment map effect
    PAINTING_ENV_MAP
};

struct Painting {
    /// Id of the painting warp node.
    /*0x00*/ PaintingData id;
    /// How many images should be drawn when the painting is rippling.
    /*0x02*/ s8 imageCount;
    /// Either PAINTING_IMAGE or PAINTING_ENV_MAP
    /*0x03*/ s8 textureType;

    /// Controls how high the peaks of the ripple are.
    /*0x04*/ f32 passiveRippleMag;
    /*0x08*/ f32 entryRippleMag;

    /// Multiplier that controls how fast the ripple regresses to the IDLE state.
    /*0x0C*/ f32 passiveRippleDecay;
    /*0x10*/ f32 entryRippleDecay;

    /// Controls the ripple's frequency
    /*0x14*/ f32 passiveRippleRate;
    /*0x18*/ f32 entryRippleRate;

    /// The rate at which the magnitude of the ripple decreases as you move farther from the central point of the ripple
    /*0x1C*/ f32 passiveDispersionFactor;
    /*0x20*/ f32 entryDispersionFactor;

    /// Display list used when the painting is normal.
    /*0x24*/ const Gfx *normalDisplayList;
    /// Data used to map the texture to the mesh
    /*0x28*/ const PaintingData *const *textureMaps;

    // Texture data
    /*0x2C*/ const Texture *const *textureArray;
    /*0x30*/ PaintingData textureWidth;
    /*0x32*/ PaintingData textureHeight;

    /// Display list used when the painting is rippling.
    /*0x34*/ const Gfx *rippleDisplayList;

    /// Controls when a passive ripple starts. RIPPLE_TRIGGER_CONTINUOUS or RIPPLE_TRIGGER_PROXIMITY.
    /*0x38*/ s8 rippleTrigger;

    /// The painting's transparency (0..255). Determines what layer the painting is in.
    /*0x39*/ Alpha alpha;

    /// Struct padding.
    /*0x3A*/ PaintingData unused;

    /// Uniformly scales the painting to a multiple of PAINTING_SIZE.
    /// By default a painting is 614.0f x 614.0f
    /*0x3C*/ f32 sizeX;
    /*0x40*/ f32 sizeY;
}; /*0x44*/

/**
 * Contains the position and normal of a vertex in the painting's generated mesh.
 */
struct PaintingMeshVertex {
    /*0x00*/ Vec3s pos;
    /*0x06*/ Vec3c norm;
}; /*0x0C*/

extern struct PaintingMeshVertex *gPaintingMesh;
extern Vec3f *gPaintingTriNorms;
extern struct Object *gRipplingPainting;
extern struct Object *gEnteredPainting;

Gfx *geo_painting_draw(s32 callContext, struct GraphNode *node, UNUSED void *context);

void bhv_painting_init(void);
void bhv_painting_loop(void);

#endif // PAINTINGS_H
