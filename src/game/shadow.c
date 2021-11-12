#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "engine/math_util.h"
#include "engine/surface_collision.h"
#include "geo_misc.h"
#include "level_table.h"
#include "memory.h"
#include "level_update.h"
#include "object_list_processor.h"
#include "rendering_graph_node.h"
#include "segment2.h"
#include "shadow.h"
#include "sm64.h"

/**
 * @file shadow.c
 * This file implements a self-contained subsystem used to draw shadows.
 */

/**
 * Encapsulation of information about a shadow.
 */
struct Shadow {
    /* The (x, y, z) position of the object whose shadow this is. */
    Vec3f parentPos;
    /* The y-position of the floor (or water or lava) underneath the object. */
    f32 floorHeight;
    /* Initial (unmodified) size of the shadow. */
    f32 shadowScale;
    /* The floor underneath the object. */
    struct Surface *floor;
    /* Angle describing "how tilted the ground is" in degrees (-90 to 90). */
    Angle floorPitch;
    /* Angle describing "which way a marble would roll," in degrees. */
    Angle floorYaw;
    /* Initial solidity of the shadow, from 0 to 255 (just an alpha value). */
    Alpha solidity;
};

enum ShadowSolidity {
    /**
     * Constant to indicate that a shadow should not be drawn.
     * This is used to disable shadows during specific frames of Mario's
     * animations.
     */
    SHADOW_SOLIDITY_NO_SHADOW,
    /**
     * Constant to indicate that a shadow's solidity has been pre-set by a previous
     * function and should not be overwritten.
     */
    SHADOW_SOILDITY_ALREADY_SET,
    /**
     * Constant to indicate that a shadow's solidity has not yet been set.
     */
    SHADOW_SOLIDITY_NOT_YET_SET,
};

/**
 * Constant to indicate any sort of circular or rectangular shadow.
 */
enum ShadowShapes {
    SHADOW_SHAPE_CIRCLE,
    SHADOW_SHAPE_SQUARE,
};

/**
 * A struct containing info about hardcoded rectangle shadows.
 */
typedef struct {
    /* Half the width of the rectangle. */
    f32 halfWidth;
    /* Half the length of the rectangle. */
    f32 halfLength;
    /* Flag for if this shadow be smaller when its object is further away. */
    u8 scaleWithDistance : 1;
} shadowRectangle;

/**
 * An array consisting of all the hardcoded rectangle shadows in the game.
 */
shadowRectangle rectangles[2] = {
    /* Shadow for Spindels. */
    { 360.0f, 230.0f, TRUE },
    /* Shadow for Whomps. */
    { 200.0f, 180.0f, TRUE }
};

// See shadow.h for documentation.
s8 gShadowFlags;

struct Shadow gCurrShadow;
#define s gCurrShadow

/**
 * Let (oldZ, oldX) be the relative coordinates of a point on a rectangle,
 * assumed to be centered at the origin on the standard SM64 X-Z plane. This
 * function will update (newZ, newX) to equal the new coordinates of that point
 * after a rotation equal to the yaw of the current graph node object.
 */
void rotate_rectangle(f32 *newZ, f32 *newX, f32 oldZ, f32 oldX) {
    struct Object *obj = (struct Object *) gCurGraphNodeObject;
    f32 sinYaw = sins(obj->oFaceAngleYaw);
    f32 cosYaw = coss(obj->oFaceAngleYaw);
    *newZ = ((oldZ * cosYaw) - (oldX * sinYaw));
    *newX = ((oldZ * sinYaw) + (oldX * cosYaw));
}

/**
 * Shrink a shadow when its parent object is further from the floor, given the
 * initial size of the shadow and the current distance.
 */
f32 scale_shadow_with_distance(f32 initial, f32 distFromFloor) {
    if (distFromFloor <= 0.0f) {
        return initial;
    } else if (distFromFloor >= 600.0f) {
        return (initial * 0.5f);
    } else {
        return (initial * (1.0f - ((distFromFloor * 0.5f) / 600.0f)));
    }
}

/**
 * Dim a shadow when its parent object is further from the ground.
 */
s32 dim_shadow_with_distance(u8 solidity, f32 distFromFloor) {
    if (solidity < 121) {
        return solidity;
    } else if (distFromFloor <=   0.0f) {
        return solidity;
    } else if (distFromFloor >= 600.0f) {
        return 120;
    } else {
        return ((((120 - solidity) * distFromFloor) / 600.0f) + (f32) solidity);
    }
}

/**
 * Return the water level below a shadow, or 0 if the water level is below
 * -10,000.
 */
f32 get_water_level_below_shadow(struct Surface **waterFloor) {
    f32 waterLevel = find_water_level_and_floor(s.parentPos[0], s.parentPos[2], waterFloor);
    if (waterLevel < FLOOR_LOWER_LIMIT_MISC) {
        return FLOOR_LOWER_LIMIT_MISC;
    } else if ((s.parentPos[1] >= waterLevel)
            && (s.floorHeight  <= waterLevel)) {
        gShadowFlags |= SHADOW_FLAG_WATER_BOX;
    }
    return waterLevel;
}

/**
 * Initialize a shadow. Return 0 on success, 1 on failure.
 *
 * @param xPos,yPos,zPos Position of the parent object (not the shadow)
 * @param shadowScale Diameter of the shadow
 * @param overwriteSolidity Flag for whether the existing shadow solidity should
 *                          be dimmed based on its distance to the floor
 */
s32 init_shadow(f32 xPos, f32 yPos, f32 zPos, s16 shadowScale, u8 overwriteSolidity) {
    struct Surface *waterFloor = NULL;

    vec3f_set(s.parentPos, xPos, yPos, zPos);

    f32 waterLevel = get_water_level_below_shadow(&waterFloor);

    // if (gEnvironmentRegions != 0) {
    //     waterLevel = get_water_level_below_shadow(s);
    // }

    Vec3f n = { s.floor->normal.x, s.floor->normal.y, s.floor->normal.z };
    // register f32 oo  = s.floor->originOffset;

    if (gShadowFlags & SHADOW_FLAG_WATER_BOX) {
        s.floorHeight = waterLevel;

        if (waterFloor != NULL) {
            s.floor = waterFloor;
            gShadowFlags &= ~SHADOW_FLAG_WATER_BOX;
            gShadowFlags |=  SHADOW_FLAG_WATER_SURFACE;
            s.solidity    = 200;
        } else {
            gShadowFlags &= ~SHADOW_FLAG_WATER_SURFACE;
            // Assume that the water is flat.
            n[0] = 0.0f;
            ((u32 *) n)[1] = FLOAT_ONE;
            n[2] = 0.0f;
            // oo = -waterLevel;
        }

    } else {
        // Don't draw a shadow if the floor is lower than expected possible,
        // or if the y-normal is negative (an unexpected result).
        if ((n[1] <= 0.0f) || (s.floorHeight < FLOOR_LOWER_LIMIT_MISC)) {
            return TRUE;
        }
    }

    f32 dy = (yPos - s.floorHeight);

    if (overwriteSolidity) {
        s.solidity = dim_shadow_with_distance(overwriteSolidity, dy);
    }

    s.shadowScale = scale_shadow_with_distance(shadowScale, dy);

    f32 floorSteepness = (sqr(n[0]) + sqr(n[2]));
    // This if-statement avoids dividing by 0.
    if (FLT_IS_NONZERO(floorSteepness)) {
        s.floorPitch = (0x4000 - atan2s(sqrtf(floorSteepness), n[1]));
        s.floorYaw   = atan2s(n[2], n[0]);
    } else {
        s.floorPitch = 0x0;
        s.floorYaw   = 0x0;
    }
    return FALSE;
}

/**
 * Given a `vertexNum` from a shadow with four vertices, update the
 * texture coordinates corresponding to that vertex. That is:
 *      0 = (-15, -15)         1 = (15, -15)
 *      2 = (-15,  15)         3 = (15,  15)
 */
void get_texture_coords(s8 vertexNum, s16 *textureX, s16 *textureY) {
#ifdef HD_SHADOWS
    *textureX = (((vertexNum & 0x1) * 127) - 63);
    *textureY = (((vertexNum >>  1) * 127) - 63);
#else
    *textureX = (((vertexNum & 0x1) *  31) - 15);
    *textureY = (((vertexNum >>  1) *  31) - 15);
#endif
}

/**
 * Make a shadow's vertex at a position relative to its parent.
 *
 * @param vertices A preallocated display list for vertices
 * @param index Index into `vertices` to insert the vertex
 * @param relX,relY,relZ Vertex position relative to its parent object
 * @param alpha Opacity of the vertex
 */
void make_shadow_vertex_at_xyz(Vtx *vertices, s8 index, f32 relX, f32 relY, f32 relZ, u8 alpha) {
    s16 vtxY = roundf(relY);
    s16 textureX = 0;
    s16 textureY = 0;

    get_texture_coords(index, &textureX, &textureY);

    // Move the shadow up slightly while standing on a flying carpet.
    if (gShadowFlags & SHADOW_FLAG_RAISED) {
        vtxY += 5;
    }

    make_vertex( // shadows are black
        vertices, index, roundf(relX), vtxY, roundf(relZ), (textureX << 5), (textureY << 5), 255, 255, 255, alpha
    );
}

/**
 * Given a shadow vertex with the given `index`, return the corresponding texture
 * coordinates ranging in the square with corners at (-1, -1), (1, -1), (-1, 1),
 * and (1, 1) in the x-z plane. See `get_texture_coords()`, which has
 * similar functionality, but return 15 times these values.
 */
void get_vertex_coords(s8 index, s8 *xCoord, s8 *zCoord) {
    *xCoord = ((index & 0x1) - 1);
    *zCoord = ((index >>  1) - 1);
    if (*xCoord == 0) *xCoord = 1;
    if (*zCoord == 0) *zCoord = 1;
}

/**
 * Populate `xPosVtx`, `yPosVtx`, and `zPosVtx` with the (x, y, z) position of the
 * shadow vertex with the given index. If the shadow is to have 9 vertices,
 * then each of those vertices is clamped down to the floor below it. Otherwise,
 * in the 4 vertex case, the vertex positions are extrapolated from the center
 * of the shadow.
 *
 * In practice, due to the if-statement in `make_shadow_vertex()`, the 9
 * vertex and 4 vertex cases are identical, and the above-described clamping
 * behavior is overwritten.
 */
void calculate_vertex_xyz(s8 index, f32 *xPosVtx, f32 *yPosVtx, f32 *zPosVtx) {
    f32 tiltedScale = (coss(s.floorPitch) * s.shadowScale);
    Angle downwardAngle = s.floorYaw;
    s8 xCoordUnit, zCoordUnit;

    // This makes xCoordUnit and yCoordUnit each one of -1, 0, or 1.
    get_vertex_coords(index, &xCoordUnit, &zCoordUnit);

    f32 halfScale       = ((xCoordUnit * s.shadowScale) / 2.0f);
    f32 halfTiltedScale = ((zCoordUnit *   tiltedScale) / 2.0f);

    f32 sinYaw = sins(downwardAngle);
    f32 cosYaw = coss(downwardAngle);

    *xPosVtx = ((halfTiltedScale * sinYaw) + (halfScale * cosYaw) + s.parentPos[0]);
    *zPosVtx = ((halfTiltedScale * cosYaw) - (halfScale * sinYaw) + s.parentPos[2]);

    if (gShadowFlags & SHADOW_FLAG_WATER_BOX) {
        *yPosVtx = s.floorHeight;
    } else {
        *yPosVtx = get_surface_height_at_location(*xPosVtx, *zPosVtx, s.floor);
    }
}

/**
 * Make a particular vertex from a shadow, calculating its position and solidity.
 */
void make_shadow_vertex(Vtx *vertices, s8 index) {
    Vec3f posVtx, rel;

    u8 solidity = s.solidity;
    if (gShadowFlags & SHADOW_FLAG_WATER_BOX) {
        solidity = 200;
    }

    calculate_vertex_xyz(index, &posVtx[0], &posVtx[1], &posVtx[2]);

    vec3f_diff(rel, posVtx, s.parentPos);

    make_shadow_vertex_at_xyz(vertices, index, rel[0], rel[1], rel[2], solidity);
}

/**
 * Add a shadow to the given display list.
 */
void add_shadow_to_display_list(Gfx *displayListHead, Vtx *verts, s8 shadowShape) {
    switch (shadowShape) {
        case SHADOW_SHAPE_CIRCLE:
            gSPDisplayList(displayListHead++, dl_shadow_circle);
            break;
        case SHADOW_SHAPE_SQUARE:
            gSPDisplayList(displayListHead++, dl_shadow_square) break;
    }
    gSPVertex(displayListHead++, verts, 4, 0);
    gSPDisplayList(displayListHead++, dl_shadow_end);
    gSPEndDisplayList(displayListHead);
}

/**
 * Linearly interpolate a shadow's solidity between zero and finalSolidity
 * depending on curr's relation to start and end.
 */
void linearly_interpolate_solidity_positive(u8 finalSolidity, s16 curr, s16 start, s16 end) {
    if ((curr >= 0) && (curr < start)) {
        s.solidity = 0;
    } else if (end < curr) {
        s.solidity = finalSolidity;
    } else {
        s.solidity = ((f32) finalSolidity * (curr - start) / (end - start));
    }
}

/**
 * Linearly interpolate a shadow's solidity between initialSolidity and zero
 * depending on curr's relation to start and end. Note that if curr < start,
 * the solidity will be zero.
 */
void linearly_interpolate_solidity_negative(u8 initialSolidity, s16 curr, s16 start, s16 end) {
    // The curr < start case is not handled. Thus, if start != 0, this function
    // will have the surprising behavior of hiding the shadow until start.
    // This is not necessarily a bug, since this function is only used once,
    // with start == 0.
    if ((curr >= start) && (end >= curr)) {
        s.solidity = ((f32) initialSolidity * (1.0f - (f32)(curr - start) / (end - start)));
    } else {
        s.solidity = 0;
    }
}

/**
 * Change a shadow's solidity based on the player's current animation frame.
 */
s32 correct_shadow_solidity_for_animations(u8 initialSolidity) {
    s16 animFrame = gMarioObject->header.gfx.animInfo.animFrame;
    switch (gMarioObject->header.gfx.animInfo.animID) {
        case MARIO_ANIM_IDLE_ON_LEDGE:
            return SHADOW_SOLIDITY_NO_SHADOW;
        case MARIO_ANIM_FAST_LEDGE_GRAB:
            linearly_interpolate_solidity_positive(initialSolidity, animFrame,  5, 14);
            return SHADOW_SOILDITY_ALREADY_SET;
        case MARIO_ANIM_SLOW_LEDGE_GRAB:
            linearly_interpolate_solidity_positive(initialSolidity, animFrame, 21, 33);
            return SHADOW_SOILDITY_ALREADY_SET;
        case MARIO_ANIM_CLIMB_DOWN_LEDGE:
            linearly_interpolate_solidity_negative(initialSolidity, animFrame,  0,  5);
            return SHADOW_SOILDITY_ALREADY_SET;
        default:
            return SHADOW_SOLIDITY_NOT_YET_SET;
    }
}

/**
 * Slightly change the height of a shadow in levels with lava.
 */
void correct_lava_shadow_height(void) {
    SurfaceType tyoe = s.floor->type;
    if ((gCurrLevelNum == LEVEL_BITFS) && (tyoe == SURFACE_BURNING)) {
        if (s.floorHeight < -3000.0f) {
            s.floorHeight = -3062.0f;
            gShadowFlags |= SHADOW_FLAG_WATER_BOX;
        } else if (s.floorHeight > 3400.0f) {
            s.floorHeight =  3492.0f;
            gShadowFlags |= SHADOW_FLAG_WATER_BOX;
        }
    } else if ((gCurrLevelNum  == LEVEL_LLL)
            && (gCurrAreaIndex == 1)
            && (tyoe  == SURFACE_BURNING)) {
        s.floorHeight = 5.0f;
        gShadowFlags |= SHADOW_FLAG_WATER_BOX;
    }
}

/**
 * Create a shadow under a player, correcting that shadow's opacity during
 * appropriate animations and other states.
 */
Gfx *create_shadow_player(f32 xPos, f32 yPos, f32 zPos, s16 shadowScale, u8 solidity) {
    s8 noShadow = FALSE;
    s32 i;

    // Update global variables about whether Mario is on a flying carpet.
    if ((gCurrLevelNum == LEVEL_RR) && (s.floor->type != SURFACE_DEATH_PLANE)) {
        switch (gFlyingCarpetState) {
            case FLYING_CARPET_MOVING_WITHOUT_MARIO:
                gShadowFlags |= (SHADOW_FLAG_ICE_CARPET | SHADOW_FLAG_RAISED);
                break;
            case FLYING_CARPET_MOVING_WITH_MARIO:
                gShadowFlags |= SHADOW_FLAG_ICE_CARPET;
                break;
        }
    }

    switch (correct_shadow_solidity_for_animations(solidity)) {
        case SHADOW_SOLIDITY_NO_SHADOW:
            return NULL;
        case SHADOW_SOILDITY_ALREADY_SET:
            noShadow = init_shadow(xPos, yPos, zPos, shadowScale, /* overwriteSolidity */ 0);
            break;
        case SHADOW_SOLIDITY_NOT_YET_SET:
            noShadow = init_shadow(xPos, yPos, zPos, shadowScale, solidity);
            break;
    }
    if (noShadow) {
        return NULL;
    }

    Vtx *verts       = alloc_display_list(4 * sizeof(Vtx));
    Gfx *displayList = alloc_display_list(5 * sizeof(Gfx));

    if ((verts == NULL) || (displayList == NULL)) {
        return NULL;
    }

    correct_lava_shadow_height();

    for (i = 0; i < 4; i++) {
        make_shadow_vertex(verts, i);
    }
    add_shadow_to_display_list(displayList, verts, SHADOW_SHAPE_CIRCLE);

    if ((verts == NULL) || (displayList == NULL)) {
        return NULL;
    }

    return displayList;
}

/**
 * Create a circular shadow composed of 4 vertices.
 */
Gfx *create_shadow_circle(f32 UNUSED xPos, f32 UNUSED yPos, f32 UNUSED zPos, s16 UNUSED shadowScale, u8 UNUSED solidity) {
    s32 i;

    if (init_shadow(xPos, yPos, zPos, shadowScale, solidity)) {
        return NULL;
    }

    Vtx *verts       = alloc_display_list(4 * sizeof(Vtx));
    Gfx *displayList = alloc_display_list(5 * sizeof(Gfx));

    if ((verts == NULL) || (displayList == NULL)) {
        return NULL;
    }

    for (i = 0; i < 4; i++) {
        make_shadow_vertex(verts, i);
    }
    add_shadow_to_display_list(displayList, verts, SHADOW_SHAPE_CIRCLE);
    return displayList;
}

/**
 * Create a circular shadow composed of 4 vertices and assume that the ground
 * underneath it is totally flat.
 */
Gfx *create_shadow_circle_assuming_flat_ground(UNUSED f32 xPos, f32 yPos, UNUSED f32 zPos, s16 shadowScale, u8 solidity) {
    f32 distBelowFloor;
    f32 floorHeight = s.floorHeight;
    f32 radius = (shadowScale / 2);

    if (floorHeight < FLOOR_LOWER_LIMIT_MISC) {
        return NULL;
    } else {
        distBelowFloor = (floorHeight - yPos);
    }

    Vtx *verts       = alloc_display_list(4 * sizeof(Vtx));
    Gfx *displayList = alloc_display_list(5 * sizeof(Gfx));

    if ((verts == NULL) || (displayList == NULL)) {
        return 0;
    }

    make_shadow_vertex_at_xyz(verts, 0, -radius, distBelowFloor, -radius, solidity);
    make_shadow_vertex_at_xyz(verts, 1,  radius, distBelowFloor, -radius, solidity);
    make_shadow_vertex_at_xyz(verts, 2, -radius, distBelowFloor,  radius, solidity);
    make_shadow_vertex_at_xyz(verts, 3,  radius, distBelowFloor,  radius, solidity);

    add_shadow_to_display_list(displayList, verts, SHADOW_SHAPE_CIRCLE);
    return displayList;
}

/**
 * Create a rectangular shadow composed of 4 vertices. This assumes the ground
 * underneath the shadow is totally flat.
 */
Gfx *create_shadow_rectangle(f32 halfWidth, f32 halfLength, f32 relY, u8 solidity) {
    Vtx *verts       = alloc_display_list(4 * sizeof(Vtx));
    Gfx *displayList = alloc_display_list(5 * sizeof(Gfx));
    f32 frontLeftX, frontLeftZ, frontRightX, frontRightZ, backLeftX, backLeftZ, backRightX, backRightZ;

    if ((verts == NULL) || (displayList == NULL)) {
        return NULL;
    }

    // Rotate the shadow based on the parent object's face angle.
    rotate_rectangle(&frontLeftZ,  &frontLeftX,  -halfLength, -halfWidth);
    rotate_rectangle(&frontRightZ, &frontRightX, -halfLength,  halfWidth);
    rotate_rectangle(&backLeftZ,   &backLeftX,    halfLength, -halfWidth);
    rotate_rectangle(&backRightZ,  &backRightX,   halfLength,  halfWidth);

    make_shadow_vertex_at_xyz(verts, 0, frontLeftX,  relY, frontLeftZ,  solidity);
    make_shadow_vertex_at_xyz(verts, 1, frontRightX, relY, frontRightZ, solidity);
    make_shadow_vertex_at_xyz(verts, 2, backLeftX,   relY, backLeftZ,   solidity);
    make_shadow_vertex_at_xyz(verts, 3, backRightX,  relY, backRightZ,  solidity);

    add_shadow_to_display_list(displayList, verts, SHADOW_SHAPE_SQUARE);
    return displayList;
}

/**
 * Populate `shadowHeight` and `solidity` appropriately; the default solidity
 * value is 200. Return 0 if a shadow should be drawn, 1 if not.
 */
s32 get_shadow_height_solidity(f32 xPos, f32 yPos, f32 zPos, f32 *shadowHeight, u8 *solidity) {
    *shadowHeight = s.floorHeight;

    if (*shadowHeight < FLOOR_LOWER_LIMIT_MISC) {
        return TRUE;
    } else {
        f32 waterLevel = find_water_level(xPos, zPos);
        if (waterLevel < FLOOR_LOWER_LIMIT_MISC) {
            // Dead if-statement. There may have been an assert here.
        } else if ((yPos >= waterLevel) && (waterLevel >= *shadowHeight)) {
            gShadowFlags |= SHADOW_FLAG_WATER_BOX;
            *shadowHeight = waterLevel;
            *solidity     = 200;
        }
    }
    return FALSE;
}

/**
 * Create a square shadow composed of 4 vertices.
 */
Gfx *create_shadow_square(f32 xPos, f32 yPos, f32 zPos, s16 shadowScale, u8 solidity, s8 shadowType) {
    f32 shadowHeight;
    f32 shadowRadius;

    if (get_shadow_height_solidity(xPos, yPos, zPos, &shadowHeight, &solidity)) {
        return NULL;
    }

    f32 distFromShadow = (yPos - shadowHeight);
    switch (shadowType) {
        case SHADOW_SQUARE_PERMANENT: shadowRadius = (shadowScale >> 1); break;
        case SHADOW_SQUARE_SCALABLE:  shadowRadius = (scale_shadow_with_distance(shadowScale, distFromShadow) * 0.5f); break;
        case SHADOW_SQUARE_TOGGLABLE: shadowRadius = ((distFromShadow >= 600.0f) ? 0.0f : (shadowScale >> 1));         break;
        default: return NULL;
    }

    return create_shadow_rectangle(shadowRadius, shadowRadius, -distFromShadow, solidity);
}

/**
 * Create a rectangular shadow whose parameters have been hardcoded in the
 * `rectangles` array.
 */
Gfx *create_shadow_hardcoded_rectangle(f32 xPos, f32 yPos, f32 zPos, UNUSED s16 shadowScale, u8 solidity, s8 shadowType) {
    f32 shadowHeight;
    f32 halfWidth, halfLength;
    s8 idx = (shadowType - SHADOW_RECTANGLE_HARDCODED_OFFSET);

    if (get_shadow_height_solidity(xPos, yPos, zPos, &shadowHeight, &solidity)) {
        return NULL;
    }

    f32 distFromShadow = (yPos - shadowHeight);
    /**
     * Note that idx could be negative or otherwise out of the bounds of
     * the `rectangles` array. In practice, it never is, because this was
     * only used twice.
     */
    if (rectangles[idx].scaleWithDistance) {
        halfWidth  = scale_shadow_with_distance(rectangles[idx].halfWidth,  distFromShadow);
        halfLength = scale_shadow_with_distance(rectangles[idx].halfLength, distFromShadow);
    } else {
        // This code is never used because the third element of the rectangle struct is always TRUE.
        halfWidth  = rectangles[idx].halfWidth;
        halfLength = rectangles[idx].halfLength;
    }

    return create_shadow_rectangle(halfWidth, halfLength, -distFromShadow, solidity);
}

/**
 * Create a shadow at the absolute position given, with the given parameters.
 * Return a pointer to the display list representing the shadow.
 */
Gfx *create_shadow_below_xyz(f32 xPos, f32 yPos, f32 zPos, s16 shadowScale, u8 shadowSolidity, s8 shadowType) {
    // Attempt to use existing floors before finding a new one.
    if (gCurGraphNodeObjectNode == gMarioObject) {
        s.floor       = gMarioState->floor;
        s.floorHeight = gMarioState->floorHeight;
    } else if ((gCurGraphNodeObject != &gMirrorMario)
            && (gCurGraphNodeObjectNode->oFloor != NULL)) {
        s.floor       = gCurGraphNodeObjectNode->oFloor;
        s.floorHeight = gCurGraphNodeObjectNode->oFloorHeight;
    } else {
        gCollisionFlags |= COLLISION_FLAG_RETURN_FIRST;
        s.floorHeight = find_floor(xPos, yPos, zPos, &s.floor);
        if (s.floor == NULL) return NULL;

    }

    gShadowFlags = SHADOW_FLAGS_NONE;
    if (s.floor->type == SURFACE_ICE) {
        gShadowFlags |= SHADOW_FLAG_ICE_CARPET;
    }

    switch (shadowType) {
        case SHADOW_CIRCLE_PLAYER:    return create_shadow_player             (xPos, yPos, zPos, shadowScale, shadowSolidity            ); break; // init
        case SHADOW_CIRCLE:           return create_shadow_circle             (xPos, yPos, zPos, shadowScale, shadowSolidity            ); break; // init
        case SHADOW_SQUARE_PERMANENT: // fallthrough
        case SHADOW_SQUARE_SCALABLE:  // fallthrough
        case SHADOW_SQUARE_TOGGLABLE: return create_shadow_square             (xPos, yPos, zPos, shadowScale, shadowSolidity, shadowType); break; // no init
        default:                      return create_shadow_hardcoded_rectangle(xPos, yPos, zPos, shadowScale, shadowSolidity, shadowType); break; // no init
    }
    return NULL;
}

#undef s
