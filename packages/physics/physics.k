// Adapted for K.
// The K standard library: `physics` (docs/stdlib.md), rigid body physics
// on Box3D: worlds, bodies, shapes of every kind Box3D has (spheres,
// capsules, convex hulls, triangle meshes, height fields, baked compounds),
// nine kinds of joints, contact, sensor, hit and move events, ray and shape
// casts and overlap queries, a character mover, recording and replay. The
// functions are C, the layer in src/physics/ over Box3D, declared here as
// extern fns against the list in src/physics/physics_api.h; the test suite
// checks the two agree.
//
// The API follows Box3D's closely, under K names: b3Body_GetPosition is
// position(body), b3CreateRevoluteJoint is create_revolute_joint. Box3D's
// definitions are structs with its defaults as field defaults, so
// `physics::BodyDef { body_type: physics::DYNAMIC, position: p }` is a whole
// definition. Box3D works in meters, kilograms and seconds, with no built-in
// up: the default gravity is -10 along y.
//
// What Box3D takes as callbacks comes back as arrays here: events and query
// results are fresh arrays, and friction and restitution mixing are picked
// from the MIX_* rules. Custom contact filters and pre-solve callbacks,
// debug drawing and the standalone dynamic tree are not part of it.
//
// Errors: a call that can fail for reasons outside the program (a file that
// is not there, points that make no hull) returns false or a zero handle,
// and error() says why. A call the program should not have made -- a
// destroyed body, a revolute function on a prismatic joint, a height field
// on a dynamic body -- is reported as it happens and aborts the program at
// the next step() or check().
//
// Physics runs on the main thread (a thread_fn reaching it is a compile
// error); Box3D spreads a step over worker threads of its own, as many as
// WorldDef.workers asks for.

namespace physics;

import std;
import vec;
import math;

// --- constants: K_PHYS_CONSTANTS in src/physics/physics_api.h ---------------

// Body types.
let STATIC: i32 = 0;
let KINEMATIC: i32 = 1;
let DYNAMIC: i32 = 2;

// Shape types.
let SHAPE_CAPSULE: i32 = 0;
let SHAPE_COMPOUND: i32 = 1;
let SHAPE_HEIGHT_FIELD: i32 = 2;
let SHAPE_HULL: i32 = 3;
let SHAPE_MESH: i32 = 4;
let SHAPE_SPHERE: i32 = 5;

// Joint types.
let JOINT_PARALLEL: i32 = 0;
let JOINT_DISTANCE: i32 = 1;
let JOINT_FILTER: i32 = 2;
let JOINT_MOTOR: i32 = 3;
let JOINT_PRISMATIC: i32 = 4;
let JOINT_REVOLUTE: i32 = 5;
let JOINT_SPHERICAL: i32 = 6;
let JOINT_WELD: i32 = 7;
let JOINT_WHEEL: i32 = 8;

// How the friction or restitution of two touching shapes combine. DEFAULT
// is Box3D's own: the geometric mean for friction, the larger restitution.
let MIX_DEFAULT: i32 = 0;
let MIX_GEOMETRIC: i32 = 1;
let MIX_MIN: i32 = 2;
let MIX_MAX: i32 = 3;
let MIX_AVERAGE: i32 = 4;
let MIX_MULTIPLY: i32 = 5;

// What time_of_impact found.
let TOI_UNKNOWN: i32 = 0;
let TOI_FAILED: i32 = 1;
let TOI_OVERLAPPED: i32 = 2;
let TOI_HIT: i32 = 3;
let TOI_SEPARATED: i32 = 4;

// Limits.
let MAX_WORKERS: i32 = 32;
let MAX_PROXY_POINTS: i32 = 128;        // points of a shape a query or cast moves
let MAX_HULL_VERTICES: i32 = 128;
let MAX_MANIFOLD_POINTS: i32 = 4;
let HEIGHT_FIELD_HOLE: i32 = 255;       // a height field cell's material index for no ground

// Every collision category, and every one matched: the default filter bits.
let ALL_BITS: u64 = 0xFFFFFFFFFFFFFFFF;

// --- handles -----------------------------------------------------------------

// Box3D's ids, whole: a destroyed object is an error to use, not a crash. A
// zero handle is none.
struct World { id: u32 }
struct Body { id: u64 }
struct Shape { id: u64 }
struct Joint { id: u64 }
// A contact between two shapes, while they touch.
struct Contact { index1: i32, world0: u16, reserved: i16, generation: u32 }

// A joint of one kind, which that kind's functions take. The functions of
// every joint take its `joint`.
struct DistanceJoint { joint: Joint }
struct FilterJoint { joint: Joint }
struct MotorJoint { joint: Joint }
struct ParallelJoint { joint: Joint }
struct PrismaticJoint { joint: Joint }
struct RevoluteJoint { joint: Joint }
struct SphericalJoint { joint: Joint }
struct WeldJoint { joint: Joint }
struct WheelJoint { joint: Joint }

// Geometry and recordings, made by the create_* functions below and owned by
// the program until it destroys them. A shape keeps what it was made from
// alive: destroying a mesh, height field or compound that shapes still use
// only lets go of the handle.
struct Hull { id: u32 }
struct Mesh { id: u32 }
struct HeightField { id: u32 }
struct Compound { id: u32 }
struct Recording { id: u32 }
struct Player { id: u32 }

// --- math --------------------------------------------------------------------

// A rotation: (x, y, z) is the axis times the sine of half the angle, w
// its cosine.
struct Quat { x: f32, y: f32, z: f32, w: f32 }
// A rigid transform: rotate by q, then move by p.
struct Transform { p: float3, q: Quat }
// A 3x3 matrix by columns, as Box3D's inertia tensors are.
struct Mat3 { cx: float3, cy: float3, cz: float3 }
struct AABB { lower: float3, upper: float3 }
// The points x with dot(normal, x) == offset.
struct Plane { normal: float3, offset: f32 }
struct MassData { mass: f32, center: float3, inertia: Mat3 }
struct Sphere { center: float3, radius: f32 }
// A segment from center1 to center2 swept by a sphere.
struct Capsule { center1: float3, center2: float3, radius: f32 }
struct Version { major: i32, minor: i32, revision: i32 }

let quat_identity = Quat { 0.0, 0.0, 0.0, 1.0 };
let transform_identity = Transform { p: float3 { 0.0, 0.0, 0.0 }, q: Quat { 0.0, 0.0, 0.0, 1.0 } };

// --- definitions: Box3D's, with its defaults ---------------------------------

// Initial capacities, to avoid growing the world's arrays as it fills.
struct Capacity {
    static_shapes: i32 = 0,
    dynamic_shapes: i32 = 0,
    static_bodies: i32 = 0,
    dynamic_bodies: i32 = 0,
    contacts: i32 = 0,
}

struct WorldDef {
    gravity: float3 = float3 { 0.0, -10.0, 0.0 },
    restitution_threshold: f32 = 1.0,   // speeds below which nothing bounces
    hit_event_threshold: f32 = 1.0,     // approach speed of a hit event
    contact_hertz: f32 = 30.0,
    contact_damping_ratio: f32 = 10.0,
    contact_speed: f32 = 3.0,           // how fast overlap is pushed out
    maximum_linear_speed: f32 = 400.0,
    enable_sleep: bool = true,
    enable_continuous: bool = true,
    // 1 steps on the calling thread; more starts Box3D's own worker threads,
    // at most MAX_WORKERS.
    workers: i32 = 1,
    friction_mixing: i32 = 0,           // MIX_DEFAULT
    restitution_mixing: i32 = 0,
    user_data: u64 = 0,
    capacity: Capacity = Capacity {},
}

struct MotionLocks {
    linear_x: bool = false,
    linear_y: bool = false,
    linear_z: bool = false,
    angular_x: bool = false,
    angular_y: bool = false,
    angular_z: bool = false,
}

struct BodyDef {
    body_type: i32 = 0,                 // STATIC
    position: float3 = float3 { 0.0, 0.0, 0.0 },
    rotation: Quat = Quat { 0.0, 0.0, 0.0, 1.0 },
    linear_velocity: float3 = float3 { 0.0, 0.0, 0.0 },
    angular_velocity: float3 = float3 { 0.0, 0.0, 0.0 },
    linear_damping: f32 = 0.0,
    angular_damping: f32 = 0.0,
    gravity_scale: f32 = 1.0,
    sleep_threshold: f32 = 0.05,        // meters per second
    // Continuous collision against tunneling, from 0.01 (safest) to 0.5.
    safety_factor: f32 = 0.5,
    motion_locks: MotionLocks = MotionLocks {},
    enable_sleep: bool = true,
    is_awake: bool = true,
    is_bullet: bool = false,            // continuous collision with dynamic bodies too
    is_enabled: bool = true,
    allow_fast_rotation: bool = false,  // for wheels
    enable_contact_recycling: bool = true,
    user_data: u64 = 0,
}

// Which shapes collide: two do when each one's category is in the other's
// mask, unless they share a group index; a positive group always collides
// and a negative one never does.
struct Filter {
    category_bits: u64 = 0xFFFFFFFFFFFFFFFF,
    mask_bits: u64 = 0xFFFFFFFFFFFFFFFF,
    group_index: i32 = 0,
}

// Which shapes a query sees: those with a category in `mask_bits` whose
// mask has one of `category_bits`.
struct QueryFilter {
    category_bits: u64 = 0xFFFFFFFFFFFFFFFF,
    mask_bits: u64 = 0xFFFFFFFFFFFFFFFF,
}

struct SurfaceMaterial {
    friction: f32 = 0.6,
    restitution: f32 = 0.0,
    rolling_resistance: f32 = 0.0,      // spheres and capsules only
    tangent_velocity: float3 = float3 { 0.0, 0.0, 0.0 },   // a conveyor belt, in the shape's frame
    user_material_id: u64 = 0,          // handed back in hits and events
    custom_color: u32 = 0,
}

struct ShapeDef {
    material: SurfaceMaterial = SurfaceMaterial {},
    density: f32 = 1000.0,              // kilograms per cubic meter: water
    filter: Filter = Filter {},
    is_sensor: bool = false,            // overlaps only, no collision response
    enable_sensor_events: bool = false,
    enable_contact_events: bool = false,
    enable_hit_events: bool = false,
    invoke_contact_creation: bool = true,
    update_body_mass: bool = true,
    enable_speculative_contact: bool = true,
    explosion_scale: f32 = 1.0,
    user_data: u64 = 0,
}

struct ExplosionDef {
    position: float3 = float3 { 0.0, 0.0, 0.0 },
    radius: f32 = 0.0,
    falloff: f32 = 0.0,
    impulse_per_area: f32 = 0.0,
    mask_bits: u64 = 0xFFFFFFFFFFFFFFFF,
}

// What every joint definition starts with: the two bodies, and the joint's
// frame in each of them (joint_def makes both from one frame in the world).
struct JointDef {
    body_a: Body = Body { 0 },
    body_b: Body = Body { 0 },
    frame_a: Transform = Transform { p: float3 { 0.0, 0.0, 0.0 }, q: Quat { 0.0, 0.0, 0.0, 1.0 } },
    frame_b: Transform = Transform { p: float3 { 0.0, 0.0, 0.0 }, q: Quat { 0.0, 0.0, 0.0, 1.0 } },
    collide_connected: bool = false,
    force_threshold: f32 = 3.4028235e38,    // joint events above these
    torque_threshold: f32 = 3.4028235e38,
    constraint_hertz: f32 = 60.0,
    constraint_damping_ratio: f32 = 2.0,
    draw_scale: f32 = 1.0,
    user_data: u64 = 0,
}

// Keeps two anchors at a distance, or within a range, rigidly or by a
// spring, optionally driven by a motor.
struct DistanceJointDef {
    base: JointDef = JointDef {},
    length: f32 = 1.0,
    enable_spring: bool = false,
    lower_spring_force: f32 = -3.4028235e38,
    upper_spring_force: f32 = 3.4028235e38,
    hertz: f32 = 0.0,
    damping_ratio: f32 = 0.0,
    enable_limit: bool = false,
    min_length: f32 = 0.0,
    max_length: f32 = 100000.0,
    enable_motor: bool = false,
    max_motor_force: f32 = 0.0,
    motor_speed: f32 = 0.0,
}

// Only stops the two bodies colliding.
struct FilterJointDef { base: JointDef = JointDef {} }

// Drives body b relative to body a, by velocity and by a spring.
struct MotorJointDef {
    base: JointDef = JointDef {},
    linear_velocity: float3 = float3 { 0.0, 0.0, 0.0 },
    max_velocity_force: f32 = 0.0,
    angular_velocity: float3 = float3 { 0.0, 0.0, 0.0 },
    max_velocity_torque: f32 = 0.0,
    linear_hertz: f32 = 0.0,
    linear_damping_ratio: f32 = 0.0,
    max_spring_force: f32 = 0.0,
    angular_hertz: f32 = 0.0,
    angular_damping_ratio: f32 = 0.0,
    max_spring_torque: f32 = 0.0,
}

// Keeps the frames' z axes parallel, by a spring.
struct ParallelJointDef {
    base: JointDef = JointDef {},
    hertz: f32 = 1.0,
    damping_ratio: f32 = 1.0,
    max_torque: f32 = 3.4028235e38,
}

// A slider along the frames' x axis.
struct PrismaticJointDef {
    base: JointDef = JointDef {},
    enable_spring: bool = false,
    hertz: f32 = 0.0,
    damping_ratio: f32 = 0.0,
    target_translation: f32 = 0.0,
    enable_limit: bool = false,
    lower_translation: f32 = 0.0,
    upper_translation: f32 = 0.0,
    enable_motor: bool = false,
    max_motor_force: f32 = 0.0,
    motor_speed: f32 = 0.0,
}

// A hinge about the frames' z axis.
struct RevoluteJointDef {
    base: JointDef = JointDef {},
    target_angle: f32 = 0.0,
    enable_spring: bool = false,
    hertz: f32 = 0.0,
    damping_ratio: f32 = 0.0,
    enable_limit: bool = false,
    lower_angle: f32 = 0.0,
    upper_angle: f32 = 0.0,
    enable_motor: bool = false,
    max_motor_torque: f32 = 0.0,
    motor_speed: f32 = 0.0,
}

// A ball and socket, with a cone and a twist limit.
struct SphericalJointDef {
    base: JointDef = JointDef {},
    enable_spring: bool = false,
    hertz: f32 = 0.0,
    damping_ratio: f32 = 0.0,
    target_rotation: Quat = Quat { 0.0, 0.0, 0.0, 1.0 },
    enable_cone_limit: bool = false,
    cone_angle: f32 = 0.0,
    enable_twist_limit: bool = false,
    lower_twist_angle: f32 = 0.0,
    upper_twist_angle: f32 = 0.0,
    enable_motor: bool = false,
    max_motor_torque: f32 = 0.0,
    motor_velocity: float3 = float3 { 0.0, 0.0, 0.0 },
}

// Glues two bodies together, rigidly with 0 hertz, else by springs.
struct WeldJointDef {
    base: JointDef = JointDef {},
    linear_hertz: f32 = 0.0,
    angular_hertz: f32 = 0.0,
    linear_damping_ratio: f32 = 0.0,
    angular_damping_ratio: f32 = 0.0,
}

// A wheel on a suspension along frame a's z axis, spinning about frame b's
// x axis, which may steer.
struct WheelJointDef {
    base: JointDef = JointDef {},
    enable_suspension_spring: bool = true,
    suspension_hertz: f32 = 1.0,
    suspension_damping_ratio: f32 = 0.7,
    enable_suspension_limit: bool = false,
    lower_suspension_limit: f32 = 0.0,
    upper_suspension_limit: f32 = 0.0,
    enable_spin_motor: bool = false,
    max_spin_torque: f32 = 0.0,
    spin_speed: f32 = 0.0,
    enable_steering: bool = false,
    steering_hertz: f32 = 1.0,
    steering_damping_ratio: f32 = 0.7,
    target_steering_angle: f32 = 0.0,
    max_steering_torque: f32 = 0.0,
    enable_steering_limit: bool = false,
    lower_steering_limit: f32 = 0.0,
    upper_steering_limit: f32 = 0.0,
}

// --- events, query results and contacts --------------------------------------

// A body that moved in the last step; fell_asleep says it then fell asleep.
struct BodyMoveEvent { transform: Transform, body: Body, user_data: u64, fell_asleep: bool }
// A shape starting or stopping overlapping a sensor.
struct SensorEvent { sensor: Shape, visitor: Shape }
// Two shapes starting or stopping touching.
struct ContactEvent { shape_a: Shape, shape_b: Shape, contact: Contact }
// Two shapes meeting faster than the world's hit_event_threshold.
struct ContactHitEvent {
    shape_a: Shape,
    shape_b: Shape,
    contact: Contact,
    point: float3,
    normal: float3,
    approach_speed: f32,
    material_a: u64,
    material_b: u64,
}
// A joint pulled harder than its force or torque threshold.
struct JointEvent { joint: Joint, user_data: u64 }

// Where a ray or a shape cast hit; `hit` is false for a miss.
struct RayHit {
    shape: Shape,
    point: float3,
    normal: float3,
    fraction: f32,              // of the translation, where it hit
    material_id: u64,
    triangle_index: i32,        // of a mesh or height field
    child_index: i32,           // of a compound
    hit: bool,
}

// A cast against one piece of geometry, in that geometry's frame.
struct CastOutput {
    normal: float3,
    point: float3,
    fraction: f32,
    iterations: i32,
    triangle_index: i32,
    child_index: i32,
    material_index: i32,
    hit: bool,
}

// A plane a character mover touches (collide_mover).
struct PlaneHit {
    shape: Shape,
    plane: Plane,
    point: float3,
    triangle_index: i32,
    child_index: i32,
    material_index: i32,
}

// A plane for solve_planes: how far it may push, and how far it has pushed.
struct CollisionPlane {
    plane: Plane,
    push_limit: f32 = 3.4028235e38,
    push: f32 = 0.0,
    clip_velocity: bool = true,
}

// A character mover hitting a body: the fraction of its sweep that is free.
struct ToiHit { point: float3, normal: float3, fraction: f32, shape: Shape }

struct DistanceOutput {
    point_a: float3,
    point_b: float3,
    normal: float3,
    distance: f32,
    iterations: i32,
}

// A body's motion over a step, for time_of_impact.
struct Sweep { local_center: float3, c1: float3, c2: float3, q1: Quat, q2: Quat }

struct ToiOutput { state: i32, point: float3, normal: float3, fraction: f32, distance: f32 }

struct ManifoldPoint {
    anchor_a: float3,           // relative to body a's center of mass
    anchor_b: float3,
    separation: f32,            // negative when overlapping
    base_separation: f32,
    normal_impulse: f32,
    total_normal_impulse: f32,
    normal_velocity: f32,
    feature_id: u32,
    triangle_index: i32,
    persisted: bool,
}

// One manifold of one contact: its points, and the contact and shapes it
// belongs to.
struct Manifold {
    contact: Contact,
    shape_a: Shape,
    shape_b: Shape,
    normal: float3,             // from a to b
    twist_impulse: f32,
    friction_impulse: float3,
    rolling_impulse: float3,
    point_count: i32,
    points: ManifoldPoint[4],
}

// Milliseconds the last step spent in each part.
struct Profile {
    step: f32, pairs: f32, collide: f32, solve: f32, solver_setup: f32, constraints: f32,
    prepare_constraints: f32, integrate_velocities: f32, warm_start: f32, solve_impulses: f32,
    integrate_positions: f32, relax_impulses: f32, apply_restitution: f32, store_impulses: f32,
    split_islands: f32, transforms: f32, sensor_hits: f32, joint_events: f32, hit_events: f32,
    refit: f32, bullets: f32, sleep_islands: f32, sensors: f32,
}

struct Counters {
    body_count: i32, shape_count: i32, contact_count: i32, joint_count: i32, island_count: i32,
    stack_used: i32, arena_capacity: i32, static_tree_height: i32, tree_height: i32,
    sat_call_count: i32, sat_cache_hit_count: i32, byte_count: i32, task_count: i32,
    color_counts: i32[24],
    manifold_counts: i32[8],
    awake_contact_count: i32, recycled_contact_count: i32,
    distance_iterations: i32, push_back_iterations: i32, root_iterations: i32,
}

// --- geometry descriptions ---------------------------------------------------

struct HullInfo {
    vertex_count: i32,
    half_edge_count: i32,       // two per edge
    face_count: i32,
    byte_count: i32,
    volume: f32,
    surface_area: f32,
    inner_radius: f32,
    center: float3,
    central_inertia: Mat3,      // of unit density, about the center
    aabb: AABB,
}

struct MeshDef {
    weld_tolerance: f32 = 0.0,
    weld_vertices: bool = false,
    use_median_split: bool = false,
    identify_edges: bool = false,       // smooths collision across shared edges
    clockwise: bool = false,            // triangles wind clockwise seen from their front
}

struct MeshInfo {
    vertex_count: i32,
    triangle_count: i32,
    material_count: i32,
    degenerate_count: i32,
    node_count: i32,
    tree_height: i32,
    byte_count: i32,
    surface_area: f32,
    bounds: AABB,
}

// count_x by count_z heights, x fastest, scaled; with min and max both 0 the
// heights' own range is kept.
struct HeightFieldDef {
    scale: float3 = float3 { 1.0, 1.0, 1.0 },
    count_x: i32 = 0,
    count_z: i32 = 0,
    min_height: f32 = 0.0,
    max_height: f32 = 0.0,
    clockwise: bool = false,
}

struct HeightFieldInfo {
    columns: i32,
    rows: i32,
    byte_count: i32,
    min_height: f32,
    max_height: f32,
    scale: float3,
    aabb: AABB,
}

// The children of a baked compound.
struct CompoundSphere { sphere: Sphere, material: SurfaceMaterial = SurfaceMaterial {} }
struct CompoundCapsule { capsule: Capsule, material: SurfaceMaterial = SurfaceMaterial {} }
struct CompoundHull {
    hull: Hull,
    transform: Transform = Transform { p: float3 { 0.0, 0.0, 0.0 },
                                       q: Quat { 0.0, 0.0, 0.0, 1.0 } },
    material: SurfaceMaterial = SurfaceMaterial {},
}
struct CompoundMesh {
    mesh: Mesh,
    transform: Transform = Transform { p: float3 { 0.0, 0.0, 0.0 },
                                       q: Quat { 0.0, 0.0, 0.0, 1.0 } },
    scale: float3 = float3 { 1.0, 1.0, 1.0 },
    material: SurfaceMaterial = SurfaceMaterial {},
}

struct CompoundInfo {
    sphere_count: i32,
    capsule_count: i32,
    hull_count: i32,
    mesh_count: i32,
    material_count: i32,
    byte_count: i32,
}

struct PlayerInfo {
    frame_count: i32,
    worker_count: i32,
    time_step: f32,
    sub_step_count: i32,
    length_scale: f32,
    bounds: AABB,
}

// --- the library -------------------------------------------------------------

// Whether the physics layer is linked in. A compiler without it refuses to
// run a physics program in-process, so this is true wherever it runs.
extern fn available() -> bool = "k_phys_available";
extern fn error_into(out: u8[:]) -> i64 = "k_phys_error";
extern fn misuse_count() -> i64 = "k_phys_misuse_count";
// Why the last call that failed did, as text.
fn error() -> u8[>..] {
    var text: u8[>..] = [];
    text.resize(1024, 0);
    let n = error_into(text);
    text.resize(min(n, 1024));
    return text;
}
// Aborts, saying why, if the program has used physics wrongly.
fn check() {
    if misuse_count() > 0 { abort(str("physics: ", error())); }
}

extern fn version() -> Version = "k_phys_version_of";
// Bytes Box3D has allocated.
extern fn byte_count() -> i64 = "k_phys_byte_count";
// The length unit, in meters, if not meters: before anything else is made.
extern fn set_length_units_per_meter(units: f32) = "k_phys_set_length_units_per_meter";
extern fn length_units_per_meter() -> f32 = "k_phys_length_units_per_meter";
extern fn world_count() -> i64 = "k_phys_world_count";
extern fn max_world_count() -> i64 = "k_phys_max_world_count";

// Box3D's own defaults, which the field defaults above repeat (for one
// length unit per meter).
extern fn default_world_def() -> WorldDef = "k_phys_default_world_def";
extern fn default_body_def() -> BodyDef = "k_phys_default_body_def";
extern fn default_shape_def() -> ShapeDef = "k_phys_default_shape_def";
extern fn default_surface_material() -> SurfaceMaterial = "k_phys_default_surface_material";
extern fn default_filter() -> Filter = "k_phys_default_filter";
extern fn default_query_filter() -> QueryFilter = "k_phys_default_query_filter";
extern fn default_explosion_def() -> ExplosionDef = "k_phys_default_explosion_def";
extern fn default_distance_joint_def() -> DistanceJointDef = "k_phys_default_distance_joint_def";
extern fn default_filter_joint_def() -> FilterJointDef = "k_phys_default_filter_joint_def";
extern fn default_motor_joint_def() -> MotorJointDef = "k_phys_default_motor_joint_def";
extern fn default_parallel_joint_def() -> ParallelJointDef = "k_phys_default_parallel_joint_def";
extern fn default_prismatic_joint_def() -> PrismaticJointDef = "k_phys_default_prismatic_joint_def";
extern fn default_revolute_joint_def() -> RevoluteJointDef = "k_phys_default_revolute_joint_def";
extern fn default_spherical_joint_def() -> SphericalJointDef = "k_phys_default_spherical_joint_def";
extern fn default_weld_joint_def() -> WeldJointDef = "k_phys_default_weld_joint_def";
extern fn default_wheel_joint_def() -> WheelJointDef = "k_phys_default_wheel_joint_def";

// --- worlds ------------------------------------------------------------------

// At most 128 worlds exist at once.
extern fn create_world(def: WorldDef) -> World = "k_phys_create_world";
fn create_world() -> World { create_world(WorldDef {}) }
extern fn destroy_raw(w: World) = "k_phys_world_destroy";
// Destroys the world and everything in it.
fn destroy(w: World) {
    check();
    destroy_raw(w);
}
extern fn is_valid(w: World) -> bool = "k_phys_world_is_valid";
// Advances by time_step seconds, solving in sub_steps parts: 1/60 and 4
// are the usual.
extern fn step_raw(w: World, time_step: f32, sub_steps: i64) = "k_phys_world_step";
fn step(w: World, time_step: f32, sub_steps: i64) {
    step_raw(w, time_step, sub_steps);
    check();
}
extern fn bounds(w: World) -> AABB = "k_phys_world_bounds";
extern fn enable_sleeping(w: World, flag: bool) = "k_phys_world_enable_sleeping";
extern fn sleeping_enabled(w: World) -> bool = "k_phys_world_sleeping_enabled";
extern fn enable_continuous(w: World, flag: bool) = "k_phys_world_enable_continuous";
extern fn continuous_enabled(w: World) -> bool = "k_phys_world_continuous_enabled";
extern fn set_restitution_threshold(w: World, value: f32) = "k_phys_world_set_restitution_threshold";
extern fn restitution_threshold(w: World) -> f32 = "k_phys_world_restitution_threshold";
extern fn set_hit_event_threshold(w: World, value: f32) = "k_phys_world_set_hit_event_threshold";
extern fn hit_event_threshold(w: World) -> f32 = "k_phys_world_hit_event_threshold";
extern fn set_gravity(w: World, gravity: float3) = "k_phys_world_set_gravity";
extern fn gravity(w: World) -> float3 = "k_phys_world_gravity";
// An impulse outward from a point, on the shapes within the radius.
extern fn explode(w: World, def: ExplosionDef) = "k_phys_world_explode";
extern fn set_contact_tuning(w: World, hertz: f32,
                                                                damping_ratio: f32,
                                                                push_speed: f32) = "k_phys_world_set_contact_tuning";
extern fn set_contact_recycle_distance(w: World,
                                                                                    distance: f32) = "k_phys_world_set_contact_recycle_distance";
extern fn contact_recycle_distance(w: World) -> f32 = "k_phys_world_contact_recycle_distance";
extern fn set_maximum_linear_speed(w: World, speed: f32) = "k_phys_world_set_maximum_linear_speed";
extern fn maximum_linear_speed(w: World) -> f32 = "k_phys_world_maximum_linear_speed";
extern fn enable_warm_starting(w: World, flag: bool) = "k_phys_world_enable_warm_starting";
extern fn warm_starting_enabled(w: World) -> bool = "k_phys_world_warm_starting_enabled";
extern fn awake_body_count(w: World) -> i64 = "k_phys_world_awake_body_count";
extern fn profile(w: World) -> Profile = "k_phys_world_profile";
extern fn counters(w: World) -> Counters = "k_phys_world_counters";
extern fn max_capacity(w: World) -> Capacity = "k_phys_world_max_capacity";
extern fn set_user_data(w: World, data: u64) = "k_phys_world_set_user_data";
extern fn user_data(w: World) -> u64 = "k_phys_world_user_data";
// A MIX_* rule.
extern fn set_friction_mixing(w: World, rule: i64) = "k_phys_world_set_friction_mixing";
extern fn set_restitution_mixing(w: World, rule: i64) = "k_phys_world_set_restitution_mixing";
extern fn set_worker_count(w: World, count: i64) = "k_phys_world_set_worker_count";
extern fn worker_count(w: World) -> i64 = "k_phys_world_worker_count";

// --- events of the last step -------------------------------------------------

// Each copies as many as fit into `out` and returns how many there are; the
// wrappers without it return them all.
extern fn body_move_events_into(w: World,
                                                                 out: BodyMoveEvent[:]) -> i64 = "k_phys_world_body_move_events";
extern fn sensor_begin_events_into(w: World,
                                                                       out: SensorEvent[:]) -> i64 = "k_phys_world_sensor_begin_events";
extern fn sensor_end_events_into(w: World,
                                                                   out: SensorEvent[:]) -> i64 = "k_phys_world_sensor_end_events";
extern fn contact_begin_events_into(
    w: World, out: ContactEvent[:]) -> i64 = "k_phys_world_contact_begin_events";
extern fn contact_end_events_into(w: World,
                                                                     out: ContactEvent[:]) -> i64 = "k_phys_world_contact_end_events";
extern fn contact_hit_events_into(
    w: World, out: ContactHitEvent[:]) -> i64 = "k_phys_world_contact_hit_events";
extern fn joint_events_into(w: World, out: JointEvent[:]) -> i64 = "k_phys_world_joint_events";

fn body_move_events(w: World) -> BodyMoveEvent[>..] {
    var out: BodyMoveEvent[>..] = [];
    let n = body_move_events_into(w, out);
    out.resize(n, default<BodyMoveEvent>());
    body_move_events_into(w, out);
    return out;
}
fn sensor_begin_events(w: World) -> SensorEvent[>..] {
    var out: SensorEvent[>..] = [];
    let n = sensor_begin_events_into(w, out);
    out.resize(n, default<SensorEvent>());
    sensor_begin_events_into(w, out);
    return out;
}
fn sensor_end_events(w: World) -> SensorEvent[>..] {
    var out: SensorEvent[>..] = [];
    let n = sensor_end_events_into(w, out);
    out.resize(n, default<SensorEvent>());
    sensor_end_events_into(w, out);
    return out;
}
fn contact_begin_events(w: World) -> ContactEvent[>..] {
    var out: ContactEvent[>..] = [];
    let n = contact_begin_events_into(w, out);
    out.resize(n, default<ContactEvent>());
    contact_begin_events_into(w, out);
    return out;
}
fn contact_end_events(w: World) -> ContactEvent[>..] {
    var out: ContactEvent[>..] = [];
    let n = contact_end_events_into(w, out);
    out.resize(n, default<ContactEvent>());
    contact_end_events_into(w, out);
    return out;
}
fn contact_hit_events(w: World) -> ContactHitEvent[>..] {
    var out: ContactHitEvent[>..] = [];
    let n = contact_hit_events_into(w, out);
    out.resize(n, default<ContactHitEvent>());
    contact_hit_events_into(w, out);
    return out;
}
fn joint_events(w: World) -> JointEvent[>..] {
    var out: JointEvent[>..] = [];
    let n = joint_events_into(w, out);
    out.resize(n, default<JointEvent>());
    joint_events_into(w, out);
    return out;
}

// --- world queries -----------------------------------------------------------

// Shapes whose bounds overlap the box.
extern fn overlap_aabb_into(w: World, box: AABB, filter: QueryFilter,
                                                         out: Shape[:]) -> i64 = "k_phys_world_overlap_aabb";
// Shapes overlapping the convex hull of `points` around `origin`, grown by
// `radius`: one point and a radius is a sphere, two a capsule.
extern fn overlap_shape_into(w: World, origin: float3,
                                                           points: const float3[:], radius: f32,
                                                           filter: QueryFilter,
                                                           out: Shape[:]) -> i64 = "k_phys_world_overlap_shape";
// Every hit along the ray, nearest first.
extern fn cast_ray_into(w: World, origin: float3, translation: float3,
                                                 filter: QueryFilter, out: RayHit[:]) -> i64 = "k_phys_world_cast_ray";
extern fn cast_ray_closest(w: World, origin: float3,
                                                            translation: float3,
                                                            filter: QueryFilter) -> RayHit = "k_phys_world_cast_ray_closest";
// Every hit of the shape swept along the translation, nearest first.
extern fn cast_shape_into(w: World, origin: float3,
                                                     points: const float3[:], radius: f32,
                                                     translation: float3, filter: QueryFilter,
                                                     out: RayHit[:]) -> i64 = "k_phys_world_cast_shape";
// How far a character's capsule moves along the translation, as a fraction,
// sliding past what it grazes.
extern fn cast_mover(w: World, origin: float3, mover: Capsule,
                                                translation: float3, filter: QueryFilter) -> f32 = "k_phys_world_cast_mover";
// The planes a character's capsule touches, for solve_planes.
extern fn collide_mover_into(w: World, origin: float3, mover: Capsule,
                                                           filter: QueryFilter,
                                                           out: PlaneHit[:]) -> i64 = "k_phys_world_collide_mover";

fn overlap_aabb(w: World, box: AABB, filter: QueryFilter) -> Shape[>..] {
    var out: Shape[>..] = [];
    let n = overlap_aabb_into(w, box, filter, out);
    out.resize(n, Shape { 0 });
    overlap_aabb_into(w, box, filter, out);
    return out;
}
fn overlap_shape(w: World, origin: float3, points: const float3[:], radius: f32,
                 filter: QueryFilter) -> Shape[>..] {
    var out: Shape[>..] = [];
    let n = overlap_shape_into(w, origin, points, radius, filter, out);
    out.resize(n, Shape { 0 });
    overlap_shape_into(w, origin, points, radius, filter, out);
    return out;
}
fn cast_ray(w: World, origin: float3, translation: float3, filter: QueryFilter) -> RayHit[>..] {
    var out: RayHit[>..] = [];
    let n = cast_ray_into(w, origin, translation, filter, out);
    out.resize(n, default<RayHit>());
    cast_ray_into(w, origin, translation, filter, out);
    return out;
}
fn cast_shape(w: World, origin: float3, points: const float3[:], radius: f32, translation: float3,
              filter: QueryFilter) -> RayHit[>..] {
    var out: RayHit[>..] = [];
    let n = cast_shape_into(w, origin, points, radius, translation, filter, out);
    out.resize(n, default<RayHit>());
    cast_shape_into(w, origin, points, radius, translation, filter, out);
    return out;
}
fn collide_mover(w: World, origin: float3, mover: Capsule, filter: QueryFilter) -> PlaneHit[>..] {
    var out: PlaneHit[>..] = [];
    let n = collide_mover_into(w, origin, mover, filter, out);
    out.resize(n, default<PlaneHit>());
    collide_mover_into(w, origin, mover, filter, out);
    return out;
}

// --- recording and replay ----------------------------------------------------

// A recording of everything a world does from start_recording to
// stop_recording, which a player replays step by step, checking that the
// replay comes out the same.
extern fn create_recording(capacity: i64) -> Recording = "k_phys_create_recording";
fn create_recording() -> Recording { create_recording(0) }
extern fn load_recording(path: const u8[:]) -> Recording = "k_phys_load_recording";
extern fn destroy(r: Recording) = "k_phys_recording_destroy";
extern fn is_valid(r: Recording) -> bool = "k_phys_recording_is_valid";
extern fn size(r: Recording) -> i64 = "k_phys_recording_size";
extern fn bytes_into(r: Recording, out: u8[:]) -> i64 = "k_phys_recording_bytes";
fn bytes(r: Recording) -> u8[>..] {
    var out: u8[>..] = [];
    out.resize(size(r), 0);
    bytes_into(r, out);
    return out;
}
extern fn save(r: Recording, path: const u8[:]) -> bool = "k_phys_recording_save";
// Replays the recording in a world of its own: whether it came out the same.
extern fn validate_replay(r: Recording, workers: i64) -> bool = "k_phys_recording_validate";
extern fn start_recording(w: World, r: Recording) = "k_phys_world_start_recording";
extern fn stop_recording(w: World) = "k_phys_world_stop_recording";

extern fn create_player(r: Recording, workers: i64) -> Player = "k_phys_create_player";
extern fn destroy(p: Player) = "k_phys_player_destroy";
extern fn is_valid(p: Player) -> bool = "k_phys_player_is_valid";
// Replays the next recorded step: false at the end. The recorded bodies
// exist once the replay has made them.
extern fn step(p: Player) -> bool = "k_phys_player_step";
// Half of a step: up to where the next step's new bodies are made, before
// they move, for drawing them where they start; the next call does the rest.
extern fn sub_step(p: Player) = "k_phys_player_sub_step";
extern fn at_pre_step(p: Player) -> bool = "k_phys_player_at_pre_step";
extern fn restart(p: Player) = "k_phys_player_restart";
extern fn seek(p: Player, frame: i64) = "k_phys_player_seek";
// The world the player replays into, whose bodies can be read like any.
extern fn world(p: Player) -> World = "k_phys_player_world";
extern fn frame(p: Player) -> i64 = "k_phys_player_frame";
extern fn frame_count(p: Player) -> i64 = "k_phys_player_frame_count";
extern fn at_end(p: Player) -> bool = "k_phys_player_at_end";
extern fn diverged(p: Player) -> bool = "k_phys_player_diverged";
extern fn diverge_frame(p: Player) -> i64 = "k_phys_player_diverge_frame";
// The recorded bodies in the order they were made, as they are now.
extern fn body_count(p: Player) -> i64 = "k_phys_player_body_count";
extern fn body(p: Player, index: i64) -> Body = "k_phys_player_body";
extern fn info(p: Player) -> PlayerInfo = "k_phys_player_info_of";
extern fn set_worker_count(p: Player, count: i64) = "k_phys_player_set_worker_count";

// --- bodies ------------------------------------------------------------------

extern fn create_body(w: World, def: BodyDef) -> Body = "k_phys_create_body";
// Destroys the body with its shapes and joints.
extern fn destroy(b: Body) = "k_phys_body_destroy";
extern fn is_valid(b: Body) -> bool = "k_phys_body_is_valid";
extern fn body_type(b: Body) -> i32 = "k_phys_body_type";
extern fn set_type(b: Body, body_type: i64) = "k_phys_body_set_type";
extern fn set_name(b: Body, name: const u8[:]) = "k_phys_body_set_name";
extern fn name_into(b: Body, out: u8[:]) -> i64 = "k_phys_body_name";
fn name(b: Body) -> u8[>..] {
    var text: u8[>..] = [];
    let n = name_into(b, text);
    text.resize(n, 0);
    name_into(b, text);
    return text;
}
extern fn set_user_data(b: Body, data: u64) = "k_phys_body_set_user_data";
extern fn user_data(b: Body) -> u64 = "k_phys_body_user_data";

// The body's origin and rotation.
extern fn position(b: Body) -> float3 = "k_phys_body_position";
extern fn rotation(b: Body) -> Quat = "k_phys_body_rotation";
extern fn transform(b: Body) -> Transform = "k_phys_body_transform";
// Each body's transform into out, which is as long: many bodies in one call.
extern fn transforms(bodies: const Body[:], out: Transform[:]) = "k_phys_body_transforms";
// Moves the body there at once: costly, and no substitute for motion.
extern fn set_transform(b: Body, position: float3, rotation: Quat) = "k_phys_body_set_transform";
extern fn local_point(b: Body, world_point: float3) -> float3 = "k_phys_body_local_point";
extern fn world_point(b: Body, local_point: float3) -> float3 = "k_phys_body_world_point";
extern fn local_vector(b: Body, world_vector: float3) -> float3 = "k_phys_body_local_vector";
extern fn world_vector(b: Body, local_vector: float3) -> float3 = "k_phys_body_world_vector";

// Of the center of mass, in the world.
extern fn linear_velocity(b: Body) -> float3 = "k_phys_body_linear_velocity";
extern fn angular_velocity(b: Body) -> float3 = "k_phys_body_angular_velocity";
extern fn set_linear_velocity(b: Body, velocity: float3) = "k_phys_body_set_linear_velocity";
extern fn set_angular_velocity(b: Body, velocity: float3) = "k_phys_body_set_angular_velocity";
// The velocity that takes a kinematic body to `target` in time_step.
extern fn set_target_transform(b: Body, target: Transform,
                                                                   time_step: f32, wake: bool) = "k_phys_body_set_target_transform";
extern fn local_point_velocity(b: Body,
                                                                   local_point: float3) -> float3 = "k_phys_body_local_point_velocity";
extern fn world_point_velocity(b: Body,
                                                                   world_point: float3) -> float3 = "k_phys_body_world_point_velocity";

// Forces and torques act over the next step; impulses change the velocity
// at once. `point` is in the world. A sleeping body ignores both unless
// woken.
extern fn apply_force(b: Body, force: float3, point: float3, wake: bool) = "k_phys_body_apply_force";
extern fn apply_force_to_center(b: Body, force: float3,
                                                                     wake: bool) = "k_phys_body_apply_force_to_center";
extern fn apply_torque(b: Body, torque: float3, wake: bool) = "k_phys_body_apply_torque";
extern fn apply_linear_impulse(b: Body, impulse: float3,
                                                                   point: float3, wake: bool) = "k_phys_body_apply_linear_impulse";
extern fn apply_linear_impulse_to_center(
    b: Body, impulse: float3, wake: bool) = "k_phys_body_apply_linear_impulse_to_center";
extern fn apply_angular_impulse(b: Body, impulse: float3,
                                                                     wake: bool) = "k_phys_body_apply_angular_impulse";

extern fn mass(b: Body) -> f32 = "k_phys_body_mass";
// In the body's frame, about its center of mass.
extern fn rotational_inertia(b: Body) -> Mat3 = "k_phys_body_rotational_inertia";
extern fn inverse_mass(b: Body) -> f32 = "k_phys_body_inverse_mass";
extern fn world_inverse_inertia(b: Body) -> Mat3 = "k_phys_body_world_inverse_inertia";
extern fn local_center(b: Body) -> float3 = "k_phys_body_local_center";
extern fn world_center(b: Body) -> float3 = "k_phys_body_world_center";
// Overrides the mass the shapes give a dynamic body, until they change.
extern fn set_mass_data(b: Body, data: MassData) = "k_phys_body_set_mass_data";
extern fn mass_data(b: Body) -> MassData = "k_phys_body_mass_data";
extern fn apply_mass_from_shapes(b: Body) = "k_phys_body_apply_mass_from_shapes";

extern fn set_linear_damping(b: Body, damping: f32) = "k_phys_body_set_linear_damping";
extern fn linear_damping(b: Body) -> f32 = "k_phys_body_linear_damping";
extern fn set_angular_damping(b: Body, damping: f32) = "k_phys_body_set_angular_damping";
extern fn angular_damping(b: Body) -> f32 = "k_phys_body_angular_damping";
extern fn set_gravity_scale(b: Body, scale: f32) = "k_phys_body_set_gravity_scale";
extern fn gravity_scale(b: Body) -> f32 = "k_phys_body_gravity_scale";
extern fn is_awake(b: Body) -> bool = "k_phys_body_is_awake";
// Wakes or sleeps the whole island the body is in.
extern fn set_awake(b: Body, awake: bool) = "k_phys_body_set_awake";
extern fn enable_sleep(b: Body, flag: bool) = "k_phys_body_enable_sleep";
extern fn sleep_enabled(b: Body) -> bool = "k_phys_body_sleep_enabled";
extern fn set_sleep_threshold(b: Body, threshold: f32) = "k_phys_body_set_sleep_threshold";
extern fn sleep_threshold(b: Body) -> f32 = "k_phys_body_sleep_threshold";
extern fn set_safety_factor(b: Body, factor: f32) = "k_phys_body_set_safety_factor";
extern fn safety_factor(b: Body) -> f32 = "k_phys_body_safety_factor";
// A disabled body is out of the simulation until enabled again.
extern fn is_enabled(b: Body) -> bool = "k_phys_body_is_enabled";
extern fn disable(b: Body) = "k_phys_body_disable";
extern fn enable(b: Body) = "k_phys_body_enable";
extern fn set_motion_locks(b: Body, locks: MotionLocks) = "k_phys_body_set_motion_locks";
extern fn motion_locks(b: Body) -> MotionLocks = "k_phys_body_motion_locks";
extern fn set_bullet(b: Body, flag: bool) = "k_phys_body_set_bullet";
extern fn is_bullet(b: Body) -> bool = "k_phys_body_is_bullet";
extern fn allow_fast_rotation(b: Body, flag: bool) = "k_phys_body_allow_fast_rotation";
extern fn fast_rotation_allowed(b: Body) -> bool = "k_phys_body_fast_rotation_allowed";
extern fn enable_contact_recycling(b: Body, flag: bool) = "k_phys_body_enable_contact_recycling";
extern fn contact_recycling_enabled(b: Body) -> bool = "k_phys_body_contact_recycling_enabled";
// On every shape of the body.
extern fn enable_hit_events(b: Body, flag: bool) = "k_phys_body_enable_hit_events";

extern fn world(b: Body) -> World = "k_phys_body_world";
extern fn shape_count(b: Body) -> i64 = "k_phys_body_shape_count";
extern fn shapes_into(b: Body, out: Shape[:]) -> i64 = "k_phys_body_shapes";
extern fn joint_count(b: Body) -> i64 = "k_phys_body_joint_count";
extern fn joints_into(b: Body, out: Joint[:]) -> i64 = "k_phys_body_joints";
// The manifolds of the body's contacts that touch.
extern fn contacts_into(b: Body, out: Manifold[:]) -> i64 = "k_phys_body_contacts";
fn shapes(b: Body) -> Shape[>..] {
    var out: Shape[>..] = [];
    out.resize(shape_count(b), Shape { 0 });
    shapes_into(b, out);
    return out;
}
fn joints(b: Body) -> Joint[>..] {
    var out: Joint[>..] = [];
    out.resize(joint_count(b), Joint { 0 });
    joints_into(b, out);
    return out;
}
fn contacts(b: Body) -> Manifold[>..] {
    var out: Manifold[>..] = [];
    let n = contacts_into(b, out);
    out.resize(n, default<Manifold>());
    contacts_into(b, out);
    return out;
}

extern fn aabb(b: Body) -> AABB = "k_phys_body_aabb";
extern fn min_extent(b: Body) -> f32 = "k_phys_body_min_extent";
extern fn max_extent(b: Body) -> float3 = "k_phys_body_max_extent";
extern fn max_extent_origin(b: Body) -> float3 = "k_phys_body_max_extent_origin";
extern fn closest_point_into(b: Body, target: float3,
                                                          result: float3&) -> f32 = "k_phys_body_closest_point";
// The point of the body's convex shapes nearest `target`, and how far.
fn closest_point(b: Body, target: float3) -> float3, f32 {
    var p = float3_0;
    let d = closest_point_into(b, target, p);
    return p, d;
}

// Queries against this body alone, placed at body_transform.
extern fn cast_ray(b: Body, origin: float3, translation: float3,
                                           filter: QueryFilter, max_fraction: f32,
                                           body_transform: Transform) -> RayHit = "k_phys_body_cast_ray";
extern fn cast_shape(b: Body, origin: float3, points: const float3[:],
                                               radius: f32, translation: float3,
                                               filter: QueryFilter, max_fraction: f32,
                                               can_encroach: bool,
                                               body_transform: Transform) -> RayHit = "k_phys_body_cast_shape";
extern fn overlap_shape(b: Body, origin: float3,
                                                     points: const float3[:], radius: f32,
                                                     filter: QueryFilter,
                                                     body_transform: Transform) -> bool = "k_phys_body_overlap_shape";
extern fn collide_mover_into(b: Body, origin: float3, mover: Capsule,
                                                          filter: QueryFilter,
                                                          body_transform: Transform,
                                                          out: PlaneHit[:]) -> i64 = "k_phys_body_collide_mover";
fn collide_mover(b: Body, origin: float3, mover: Capsule, filter: QueryFilter,
                 body_transform: Transform) -> PlaneHit[>..] {
    var out: PlaneHit[>..] = [];
    let n = collide_mover_into(b, origin, mover, filter, body_transform, out);
    out.resize(n, default<PlaneHit>());
    collide_mover_into(b, origin, mover, filter, body_transform, out);
    return out;
}
// The first time a mover's capsule hits the body as the body moves from
// transform1 to transform2.
extern fn time_of_impact_mover(b: Body, origin: float3,
                                                                   mover: Capsule,
                                                                   translation: float3,
                                                                   filter: QueryFilter,
                                                                   transform1: Transform,
                                                                   transform2: Transform) -> ToiHit = "k_phys_body_time_of_impact_mover";

// --- shapes ------------------------------------------------------------------

// A shape's geometry is in its body's frame.
extern fn create_sphere_shape(b: Body, def: ShapeDef,
                                                            sphere: Sphere) -> Shape = "k_phys_create_sphere_shape";
extern fn create_capsule_shape(b: Body, def: ShapeDef,
                                                              capsule: Capsule) -> Shape = "k_phys_create_capsule_shape";
// A box of these half extents, placed by frame.
extern fn create_box_shape(b: Body, def: ShapeDef, half_extents: float3,
                                                      frame: Transform) -> Shape = "k_phys_create_box_shape";
fn create_box_shape(b: Body, def: ShapeDef, half_extents: float3) -> Shape {
    create_box_shape(b, def, half_extents, transform_identity)
}
extern fn create_hull_shape(b: Body, def: ShapeDef,
                                                        hull: Hull) -> Shape = "k_phys_create_hull_shape";
// The hull scaled, then transformed; a scale may mirror.
extern fn create_transformed_hull_shape(
    b: Body, def: ShapeDef, hull: Hull, transform: Transform, scale: float3) -> Shape = "k_phys_create_transformed_hull_shape";
// A mesh collides only with what is not a mesh. `materials` are by each
// triangle's material index; none is the definition's for all.
extern fn create_mesh_shape(
    b: Body, def: ShapeDef, mesh: Mesh, scale: float3,
    materials: const SurfaceMaterial[:]) -> Shape = "k_phys_create_mesh_shape";
fn create_mesh_shape(b: Body, def: ShapeDef, mesh: Mesh) -> Shape {
    let none: SurfaceMaterial[>..] = [];
    create_mesh_shape(b, def, mesh, float3_1, none)
}
// Height fields and compounds go on static bodies.
extern fn create_height_field_shape(
    b: Body, def: ShapeDef, height_field: HeightField,
    materials: const SurfaceMaterial[:]) -> Shape = "k_phys_create_height_field_shape";
fn create_height_field_shape(b: Body, def: ShapeDef, height_field: HeightField) -> Shape {
    let none: SurfaceMaterial[>..] = [];
    create_height_field_shape(b, def, height_field, none)
}
extern fn create_compound_shape(b: Body, def: ShapeDef,
                                                                compound: Compound) -> Shape = "k_phys_create_compound_shape";

extern fn destroy(s: Shape, update_body_mass: bool) = "k_phys_shape_destroy";
fn destroy(s: Shape) { destroy(s, true); }
extern fn is_valid(s: Shape) -> bool = "k_phys_shape_is_valid";
extern fn shape_type(s: Shape) -> i32 = "k_phys_shape_type";
extern fn body(s: Shape) -> Body = "k_phys_shape_body";
extern fn world(s: Shape) -> World = "k_phys_shape_world";
extern fn is_sensor(s: Shape) -> bool = "k_phys_shape_is_sensor";
extern fn set_name(s: Shape, name: const u8[:]) = "k_phys_shape_set_name";
extern fn name_into(s: Shape, out: u8[:]) -> i64 = "k_phys_shape_name";
fn name(s: Shape) -> u8[>..] {
    var text: u8[>..] = [];
    let n = name_into(s, text);
    text.resize(n, 0);
    name_into(s, text);
    return text;
}
extern fn set_user_data(s: Shape, data: u64) = "k_phys_shape_set_user_data";
extern fn user_data(s: Shape) -> u64 = "k_phys_shape_user_data";
extern fn set_density(s: Shape, density: f32, update_body_mass: bool) = "k_phys_shape_set_density";
extern fn density(s: Shape) -> f32 = "k_phys_shape_density";
extern fn set_friction(s: Shape, friction: f32) = "k_phys_shape_set_friction";
extern fn friction(s: Shape) -> f32 = "k_phys_shape_friction";
extern fn set_restitution(s: Shape, restitution: f32) = "k_phys_shape_set_restitution";
extern fn restitution(s: Shape) -> f32 = "k_phys_shape_restitution";
extern fn set_surface_material(s: Shape,
                                                                    material: SurfaceMaterial) = "k_phys_shape_set_surface_material";
extern fn surface_material(s: Shape) -> SurfaceMaterial = "k_phys_shape_surface_material";
// A mesh shape's per-triangle materials.
extern fn mesh_material_count(s: Shape) -> i64 = "k_phys_shape_mesh_material_count";
extern fn set_mesh_material(s: Shape, material: SurfaceMaterial,
                                                              index: i64) = "k_phys_shape_set_mesh_material";
extern fn mesh_material(s: Shape, index: i64) -> SurfaceMaterial = "k_phys_shape_mesh_material";
extern fn shape_filter(s: Shape) -> Filter = "k_phys_shape_filter";
// Costly: with invoke_contacts, the shape's contacts are made again.
extern fn set_shape_filter(s: Shape, filter: Filter,
                                                      invoke_contacts: bool) = "k_phys_shape_set_filter";
extern fn enable_sensor_events(s: Shape, flag: bool) = "k_phys_shape_enable_sensor_events";
extern fn sensor_events_enabled(s: Shape) -> bool = "k_phys_shape_sensor_events_enabled";
extern fn enable_contact_events(s: Shape, flag: bool) = "k_phys_shape_enable_contact_events";
extern fn contact_events_enabled(s: Shape) -> bool = "k_phys_shape_contact_events_enabled";
extern fn enable_hit_events(s: Shape, flag: bool) = "k_phys_shape_enable_hit_events";
extern fn hit_events_enabled(s: Shape) -> bool = "k_phys_shape_hit_events_enabled";
// A ray against this shape where it is now, in world coordinates.
extern fn ray_cast(s: Shape, origin: float3,
                                            translation: float3) -> CastOutput = "k_phys_shape_ray_cast";

// The shape's geometry, for a shape of that kind. hull() is a copy of its
// own, for the program to destroy.
extern fn sphere(s: Shape) -> Sphere = "k_phys_shape_sphere";
extern fn capsule(s: Shape) -> Capsule = "k_phys_shape_capsule";
extern fn hull(s: Shape) -> Hull = "k_phys_shape_hull";
extern fn mesh(s: Shape) -> Mesh = "k_phys_shape_mesh";
extern fn mesh_scale(s: Shape) -> float3 = "k_phys_shape_mesh_scale";
extern fn height_field(s: Shape) -> HeightField = "k_phys_shape_height_field";
extern fn compound(s: Shape) -> Compound = "k_phys_shape_compound";
// New geometry for the shape, which may change its kind; the body's mass
// stays as it was until apply_mass_from_shapes.
extern fn set_sphere(s: Shape, sphere: Sphere) = "k_phys_shape_set_sphere";
extern fn set_capsule(s: Shape, capsule: Capsule) = "k_phys_shape_set_capsule";
extern fn set_hull(s: Shape, hull: Hull) = "k_phys_shape_set_hull";
extern fn set_mesh(s: Shape, mesh: Mesh, scale: float3) = "k_phys_shape_set_mesh";

extern fn contacts_into(s: Shape, out: Manifold[:]) -> i64 = "k_phys_shape_contacts";
// The shapes a sensor overlaps now.
extern fn sensor_overlaps_into(s: Shape, out: Shape[:]) -> i64 = "k_phys_shape_sensor_overlaps";
fn contacts(s: Shape) -> Manifold[>..] {
    var out: Manifold[>..] = [];
    let n = contacts_into(s, out);
    out.resize(n, default<Manifold>());
    contacts_into(s, out);
    return out;
}
fn sensor_overlaps(s: Shape) -> Shape[>..] {
    var out: Shape[>..] = [];
    let n = sensor_overlaps_into(s, out);
    out.resize(n, Shape { 0 });
    sensor_overlaps_into(s, out);
    return out;
}
extern fn aabb(s: Shape) -> AABB = "k_phys_shape_aabb";
extern fn mass_data(s: Shape) -> MassData = "k_phys_shape_mass_data";
extern fn closest_point(s: Shape, target: float3) -> float3 = "k_phys_shape_closest_point";
// Drag and lift from air moving at `wind`, on the shape's body.
extern fn apply_wind(s: Shape, wind: float3, drag: f32, lift: f32,
                                                max_speed: f32, wake: bool) = "k_phys_shape_apply_wind";

// --- joints ------------------------------------------------------------------

extern fn create_distance_joint(
    w: World, def: DistanceJointDef) -> DistanceJoint = "k_phys_create_distance_joint";
extern fn create_filter_joint(w: World,
                                                            def: FilterJointDef) -> FilterJoint = "k_phys_create_filter_joint";
extern fn create_motor_joint(w: World,
                                                          def: MotorJointDef) -> MotorJoint = "k_phys_create_motor_joint";
extern fn create_parallel_joint(
    w: World, def: ParallelJointDef) -> ParallelJoint = "k_phys_create_parallel_joint";
extern fn create_prismatic_joint(
    w: World, def: PrismaticJointDef) -> PrismaticJoint = "k_phys_create_prismatic_joint";
extern fn create_revolute_joint(
    w: World, def: RevoluteJointDef) -> RevoluteJoint = "k_phys_create_revolute_joint";
extern fn create_spherical_joint(
    w: World, def: SphericalJointDef) -> SphericalJoint = "k_phys_create_spherical_joint";
extern fn create_weld_joint(w: World, def: WeldJointDef) -> WeldJoint = "k_phys_create_weld_joint";
extern fn create_wheel_joint(w: World,
                                                          def: WheelJointDef) -> WheelJoint = "k_phys_create_wheel_joint";

// Every joint's own: pass a kind's `joint`.
extern fn destroy(j: Joint, wake_bodies: bool) = "k_phys_joint_destroy";
fn destroy(j: Joint) { destroy(j, true); }
extern fn is_valid(j: Joint) -> bool = "k_phys_joint_is_valid";
extern fn joint_type(j: Joint) -> i32 = "k_phys_joint_type";
extern fn body_a(j: Joint) -> Body = "k_phys_joint_body_a";
extern fn body_b(j: Joint) -> Body = "k_phys_joint_body_b";
extern fn world(j: Joint) -> World = "k_phys_joint_world";
extern fn set_local_frame_a(j: Joint, frame: Transform) = "k_phys_joint_set_local_frame_a";
extern fn local_frame_a(j: Joint) -> Transform = "k_phys_joint_local_frame_a";
extern fn set_local_frame_b(j: Joint, frame: Transform) = "k_phys_joint_set_local_frame_b";
extern fn local_frame_b(j: Joint) -> Transform = "k_phys_joint_local_frame_b";
extern fn set_collide_connected(j: Joint, flag: bool) = "k_phys_joint_set_collide_connected";
extern fn collide_connected(j: Joint) -> bool = "k_phys_joint_collide_connected";
extern fn set_user_data(j: Joint, data: u64) = "k_phys_joint_set_user_data";
extern fn user_data(j: Joint) -> u64 = "k_phys_joint_user_data";
extern fn wake_bodies(j: Joint) = "k_phys_joint_wake_bodies";
extern fn is_awake(j: Joint) -> bool = "k_phys_joint_is_awake";
// What the joint applies to body b, in the world.
extern fn constraint_force(j: Joint) -> float3 = "k_phys_joint_constraint_force";
extern fn constraint_torque(j: Joint) -> float3 = "k_phys_joint_constraint_torque";
extern fn linear_separation(j: Joint) -> f32 = "k_phys_joint_linear_separation";
extern fn angular_separation(j: Joint) -> f32 = "k_phys_joint_angular_separation";
extern fn set_constraint_tuning(j: Joint, hertz: f32,
                                                                      damping_ratio: f32) = "k_phys_joint_set_constraint_tuning";
extern fn constraint_tuning_raw(j: Joint) -> float2 = "k_phys_joint_constraint_tuning";
// Hertz and damping ratio.
fn constraint_tuning(j: Joint) -> f32, f32 {
    let t = constraint_tuning_raw(j);
    return t.x, t.y;
}
extern fn set_force_threshold(j: Joint, threshold: f32) = "k_phys_joint_set_force_threshold";
extern fn force_threshold(j: Joint) -> f32 = "k_phys_joint_force_threshold";
extern fn set_torque_threshold(j: Joint, threshold: f32) = "k_phys_joint_set_torque_threshold";
extern fn torque_threshold(j: Joint) -> f32 = "k_phys_joint_torque_threshold";

// Distance joints.
extern fn set_rest_length(j: DistanceJoint, length: f32) = "k_phys_distance_set_rest_length";
extern fn rest_length(j: DistanceJoint) -> f32 = "k_phys_distance_rest_length";
extern fn enable_spring(j: DistanceJoint, flag: bool) = "k_phys_distance_enable_spring";
extern fn spring_enabled(j: DistanceJoint) -> bool = "k_phys_distance_spring_enabled";
extern fn set_spring_force_range(j: DistanceJoint,
                                                                           lower: f32, upper: f32) = "k_phys_distance_set_spring_force_range";
extern fn spring_force_range_raw(j: DistanceJoint) -> float2 = "k_phys_distance_spring_force_range";
fn spring_force_range(j: DistanceJoint) -> f32, f32 {
    let r = spring_force_range_raw(j);
    return r.x, r.y;
}
extern fn set_spring_hertz(j: DistanceJoint, hertz: f32) = "k_phys_distance_set_spring_hertz";
extern fn spring_hertz(j: DistanceJoint) -> f32 = "k_phys_distance_spring_hertz";
extern fn set_spring_damping_ratio(j: DistanceJoint,
                                                                               ratio: f32) = "k_phys_distance_set_spring_damping_ratio";
extern fn spring_damping_ratio(j: DistanceJoint) -> f32 = "k_phys_distance_spring_damping_ratio";
extern fn enable_limit(j: DistanceJoint, flag: bool) = "k_phys_distance_enable_limit";
extern fn limit_enabled(j: DistanceJoint) -> bool = "k_phys_distance_limit_enabled";
extern fn set_length_range(j: DistanceJoint, min_length: f32,
                                                               max_length: f32) = "k_phys_distance_set_length_range";
extern fn min_length(j: DistanceJoint) -> f32 = "k_phys_distance_min_length";
extern fn max_length(j: DistanceJoint) -> f32 = "k_phys_distance_max_length";
extern fn current_length(j: DistanceJoint) -> f32 = "k_phys_distance_current_length";
extern fn enable_motor(j: DistanceJoint, flag: bool) = "k_phys_distance_enable_motor";
extern fn motor_enabled(j: DistanceJoint) -> bool = "k_phys_distance_motor_enabled";
extern fn set_motor_speed(j: DistanceJoint, speed: f32) = "k_phys_distance_set_motor_speed";
extern fn motor_speed(j: DistanceJoint) -> f32 = "k_phys_distance_motor_speed";
extern fn set_max_motor_force(j: DistanceJoint, force: f32) = "k_phys_distance_set_max_motor_force";
extern fn max_motor_force(j: DistanceJoint) -> f32 = "k_phys_distance_max_motor_force";
extern fn motor_force(j: DistanceJoint) -> f32 = "k_phys_distance_motor_force";

// Motor joints.
extern fn set_linear_velocity(j: MotorJoint, velocity: float3) = "k_phys_motor_set_linear_velocity";
extern fn linear_velocity(j: MotorJoint) -> float3 = "k_phys_motor_linear_velocity";
extern fn set_angular_velocity(j: MotorJoint,
                                                                    velocity: float3) = "k_phys_motor_set_angular_velocity";
extern fn angular_velocity(j: MotorJoint) -> float3 = "k_phys_motor_angular_velocity";
extern fn set_max_velocity_force(j: MotorJoint, force: f32) = "k_phys_motor_set_max_velocity_force";
extern fn max_velocity_force(j: MotorJoint) -> f32 = "k_phys_motor_max_velocity_force";
extern fn set_max_velocity_torque(j: MotorJoint,
                                                                          torque: f32) = "k_phys_motor_set_max_velocity_torque";
extern fn max_velocity_torque(j: MotorJoint) -> f32 = "k_phys_motor_max_velocity_torque";
extern fn set_linear_hertz(j: MotorJoint, hertz: f32) = "k_phys_motor_set_linear_hertz";
extern fn linear_hertz(j: MotorJoint) -> f32 = "k_phys_motor_linear_hertz";
extern fn set_linear_damping_ratio(j: MotorJoint,
                                                                            ratio: f32) = "k_phys_motor_set_linear_damping_ratio";
extern fn linear_damping_ratio(j: MotorJoint) -> f32 = "k_phys_motor_linear_damping_ratio";
extern fn set_angular_hertz(j: MotorJoint, hertz: f32) = "k_phys_motor_set_angular_hertz";
extern fn angular_hertz(j: MotorJoint) -> f32 = "k_phys_motor_angular_hertz";
extern fn set_angular_damping_ratio(j: MotorJoint,
                                                                              ratio: f32) = "k_phys_motor_set_angular_damping_ratio";
extern fn angular_damping_ratio(j: MotorJoint) -> f32 = "k_phys_motor_angular_damping_ratio";
extern fn set_max_spring_force(j: MotorJoint, force: f32) = "k_phys_motor_set_max_spring_force";
extern fn max_spring_force(j: MotorJoint) -> f32 = "k_phys_motor_max_spring_force";
extern fn set_max_spring_torque(j: MotorJoint, torque: f32) = "k_phys_motor_set_max_spring_torque";
extern fn max_spring_torque(j: MotorJoint) -> f32 = "k_phys_motor_max_spring_torque";

// Parallel joints.
extern fn set_spring_hertz(j: ParallelJoint, hertz: f32) = "k_phys_parallel_set_spring_hertz";
extern fn spring_hertz(j: ParallelJoint) -> f32 = "k_phys_parallel_spring_hertz";
extern fn set_spring_damping_ratio(j: ParallelJoint,
                                                                               ratio: f32) = "k_phys_parallel_set_spring_damping_ratio";
extern fn spring_damping_ratio(j: ParallelJoint) -> f32 = "k_phys_parallel_spring_damping_ratio";
extern fn set_max_torque(j: ParallelJoint, torque: f32) = "k_phys_parallel_set_max_torque";
extern fn max_torque(j: ParallelJoint) -> f32 = "k_phys_parallel_max_torque";

// Prismatic joints.
extern fn enable_spring(j: PrismaticJoint, flag: bool) = "k_phys_prismatic_enable_spring";
extern fn spring_enabled(j: PrismaticJoint) -> bool = "k_phys_prismatic_spring_enabled";
extern fn set_spring_hertz(j: PrismaticJoint, hertz: f32) = "k_phys_prismatic_set_spring_hertz";
extern fn spring_hertz(j: PrismaticJoint) -> f32 = "k_phys_prismatic_spring_hertz";
extern fn set_spring_damping_ratio(j: PrismaticJoint,
                                                                                ratio: f32) = "k_phys_prismatic_set_spring_damping_ratio";
extern fn spring_damping_ratio(j: PrismaticJoint) -> f32 = "k_phys_prismatic_spring_damping_ratio";
extern fn set_target_translation(j: PrismaticJoint,
                                                                            translation: f32) = "k_phys_prismatic_set_target_translation";
extern fn target_translation(j: PrismaticJoint) -> f32 = "k_phys_prismatic_target_translation";
extern fn enable_limit(j: PrismaticJoint, flag: bool) = "k_phys_prismatic_enable_limit";
extern fn limit_enabled(j: PrismaticJoint) -> bool = "k_phys_prismatic_limit_enabled";
extern fn lower_limit(j: PrismaticJoint) -> f32 = "k_phys_prismatic_lower_limit";
extern fn upper_limit(j: PrismaticJoint) -> f32 = "k_phys_prismatic_upper_limit";
extern fn set_limits(j: PrismaticJoint, lower: f32, upper: f32) = "k_phys_prismatic_set_limits";
extern fn enable_motor(j: PrismaticJoint, flag: bool) = "k_phys_prismatic_enable_motor";
extern fn motor_enabled(j: PrismaticJoint) -> bool = "k_phys_prismatic_motor_enabled";
extern fn set_motor_speed(j: PrismaticJoint, speed: f32) = "k_phys_prismatic_set_motor_speed";
extern fn motor_speed(j: PrismaticJoint) -> f32 = "k_phys_prismatic_motor_speed";
extern fn set_max_motor_force(j: PrismaticJoint,
                                                                      force: f32) = "k_phys_prismatic_set_max_motor_force";
extern fn max_motor_force(j: PrismaticJoint) -> f32 = "k_phys_prismatic_max_motor_force";
extern fn motor_force(j: PrismaticJoint) -> f32 = "k_phys_prismatic_motor_force";
extern fn translation(j: PrismaticJoint) -> f32 = "k_phys_prismatic_translation";
extern fn speed(j: PrismaticJoint) -> f32 = "k_phys_prismatic_speed";

// Revolute joints.
extern fn enable_spring(j: RevoluteJoint, flag: bool) = "k_phys_revolute_enable_spring";
extern fn spring_enabled(j: RevoluteJoint) -> bool = "k_phys_revolute_spring_enabled";
extern fn set_spring_hertz(j: RevoluteJoint, hertz: f32) = "k_phys_revolute_set_spring_hertz";
extern fn spring_hertz(j: RevoluteJoint) -> f32 = "k_phys_revolute_spring_hertz";
extern fn set_spring_damping_ratio(j: RevoluteJoint,
                                                                               ratio: f32) = "k_phys_revolute_set_spring_damping_ratio";
extern fn spring_damping_ratio(j: RevoluteJoint) -> f32 = "k_phys_revolute_spring_damping_ratio";
extern fn set_target_angle(j: RevoluteJoint, radians: f32) = "k_phys_revolute_set_target_angle";
extern fn target_angle(j: RevoluteJoint) -> f32 = "k_phys_revolute_target_angle";
extern fn angle(j: RevoluteJoint) -> f32 = "k_phys_revolute_angle";
extern fn enable_limit(j: RevoluteJoint, flag: bool) = "k_phys_revolute_enable_limit";
extern fn limit_enabled(j: RevoluteJoint) -> bool = "k_phys_revolute_limit_enabled";
extern fn lower_limit(j: RevoluteJoint) -> f32 = "k_phys_revolute_lower_limit";
extern fn upper_limit(j: RevoluteJoint) -> f32 = "k_phys_revolute_upper_limit";
extern fn set_limits(j: RevoluteJoint, lower: f32, upper: f32) = "k_phys_revolute_set_limits";
extern fn enable_motor(j: RevoluteJoint, flag: bool) = "k_phys_revolute_enable_motor";
extern fn motor_enabled(j: RevoluteJoint) -> bool = "k_phys_revolute_motor_enabled";
extern fn set_motor_speed(j: RevoluteJoint, speed: f32) = "k_phys_revolute_set_motor_speed";
extern fn motor_speed(j: RevoluteJoint) -> f32 = "k_phys_revolute_motor_speed";
extern fn motor_torque(j: RevoluteJoint) -> f32 = "k_phys_revolute_motor_torque";
extern fn set_max_motor_torque(j: RevoluteJoint,
                                                                       torque: f32) = "k_phys_revolute_set_max_motor_torque";
extern fn max_motor_torque(j: RevoluteJoint) -> f32 = "k_phys_revolute_max_motor_torque";

// Spherical joints.
extern fn enable_cone_limit(j: SphericalJoint, flag: bool) = "k_phys_spherical_enable_cone_limit";
extern fn cone_limit_enabled(j: SphericalJoint) -> bool = "k_phys_spherical_cone_limit_enabled";
extern fn cone_limit(j: SphericalJoint) -> f32 = "k_phys_spherical_cone_limit";
extern fn set_cone_limit(j: SphericalJoint, radians: f32) = "k_phys_spherical_set_cone_limit";
extern fn cone_angle(j: SphericalJoint) -> f32 = "k_phys_spherical_cone_angle";
extern fn enable_twist_limit(j: SphericalJoint, flag: bool) = "k_phys_spherical_enable_twist_limit";
extern fn twist_limit_enabled(j: SphericalJoint) -> bool = "k_phys_spherical_twist_limit_enabled";
extern fn lower_twist_limit(j: SphericalJoint) -> f32 = "k_phys_spherical_lower_twist_limit";
extern fn upper_twist_limit(j: SphericalJoint) -> f32 = "k_phys_spherical_upper_twist_limit";
extern fn set_twist_limits(j: SphericalJoint, lower: f32,
                                                                upper: f32) = "k_phys_spherical_set_twist_limits";
extern fn twist_angle(j: SphericalJoint) -> f32 = "k_phys_spherical_twist_angle";
extern fn enable_spring(j: SphericalJoint, flag: bool) = "k_phys_spherical_enable_spring";
extern fn spring_enabled(j: SphericalJoint) -> bool = "k_phys_spherical_spring_enabled";
extern fn set_spring_hertz(j: SphericalJoint, hertz: f32) = "k_phys_spherical_set_spring_hertz";
extern fn spring_hertz(j: SphericalJoint) -> f32 = "k_phys_spherical_spring_hertz";
extern fn set_spring_damping_ratio(j: SphericalJoint,
                                                                                ratio: f32) = "k_phys_spherical_set_spring_damping_ratio";
extern fn spring_damping_ratio(j: SphericalJoint) -> f32 = "k_phys_spherical_spring_damping_ratio";
extern fn set_target_rotation(j: SphericalJoint,
                                                                      rotation: Quat) = "k_phys_spherical_set_target_rotation";
extern fn target_rotation(j: SphericalJoint) -> Quat = "k_phys_spherical_target_rotation";
extern fn enable_motor(j: SphericalJoint, flag: bool) = "k_phys_spherical_enable_motor";
extern fn motor_enabled(j: SphericalJoint) -> bool = "k_phys_spherical_motor_enabled";
extern fn set_motor_velocity(j: SphericalJoint,
                                                                    velocity: float3) = "k_phys_spherical_set_motor_velocity";
extern fn motor_velocity(j: SphericalJoint) -> float3 = "k_phys_spherical_motor_velocity";
extern fn motor_torque(j: SphericalJoint) -> float3 = "k_phys_spherical_motor_torque";
extern fn set_max_motor_torque(j: SphericalJoint,
                                                                        torque: f32) = "k_phys_spherical_set_max_motor_torque";
extern fn max_motor_torque(j: SphericalJoint) -> f32 = "k_phys_spherical_max_motor_torque";

// Weld joints.
extern fn set_linear_hertz(j: WeldJoint, hertz: f32) = "k_phys_weld_set_linear_hertz";
extern fn linear_hertz(j: WeldJoint) -> f32 = "k_phys_weld_linear_hertz";
extern fn set_linear_damping_ratio(j: WeldJoint,
                                                                           ratio: f32) = "k_phys_weld_set_linear_damping_ratio";
extern fn linear_damping_ratio(j: WeldJoint) -> f32 = "k_phys_weld_linear_damping_ratio";
extern fn set_angular_hertz(j: WeldJoint, hertz: f32) = "k_phys_weld_set_angular_hertz";
extern fn angular_hertz(j: WeldJoint) -> f32 = "k_phys_weld_angular_hertz";
extern fn set_angular_damping_ratio(j: WeldJoint,
                                                                             ratio: f32) = "k_phys_weld_set_angular_damping_ratio";
extern fn angular_damping_ratio(j: WeldJoint) -> f32 = "k_phys_weld_angular_damping_ratio";

// Wheel joints.
extern fn enable_suspension(j: WheelJoint, flag: bool) = "k_phys_wheel_enable_suspension";
extern fn suspension_enabled(j: WheelJoint) -> bool = "k_phys_wheel_suspension_enabled";
extern fn set_suspension_hertz(j: WheelJoint, hertz: f32) = "k_phys_wheel_set_suspension_hertz";
extern fn suspension_hertz(j: WheelJoint) -> f32 = "k_phys_wheel_suspension_hertz";
extern fn set_suspension_damping_ratio(j: WheelJoint,
                                                                                    ratio: f32) = "k_phys_wheel_set_suspension_damping_ratio";
extern fn suspension_damping_ratio(j: WheelJoint) -> f32 = "k_phys_wheel_suspension_damping_ratio";
extern fn enable_suspension_limit(j: WheelJoint,
                                                                          flag: bool) = "k_phys_wheel_enable_suspension_limit";
extern fn suspension_limit_enabled(j: WheelJoint) -> bool = "k_phys_wheel_suspension_limit_enabled";
extern fn lower_suspension_limit(j: WheelJoint) -> f32 = "k_phys_wheel_lower_suspension_limit";
extern fn upper_suspension_limit(j: WheelJoint) -> f32 = "k_phys_wheel_upper_suspension_limit";
extern fn set_suspension_limits(j: WheelJoint, lower: f32,
                                                                      upper: f32) = "k_phys_wheel_set_suspension_limits";
extern fn enable_spin_motor(j: WheelJoint, flag: bool) = "k_phys_wheel_enable_spin_motor";
extern fn spin_motor_enabled(j: WheelJoint) -> bool = "k_phys_wheel_spin_motor_enabled";
extern fn set_spin_motor_speed(j: WheelJoint, speed: f32) = "k_phys_wheel_set_spin_motor_speed";
extern fn spin_motor_speed(j: WheelJoint) -> f32 = "k_phys_wheel_spin_motor_speed";
extern fn set_max_spin_torque(j: WheelJoint, torque: f32) = "k_phys_wheel_set_max_spin_torque";
extern fn max_spin_torque(j: WheelJoint) -> f32 = "k_phys_wheel_max_spin_torque";
extern fn spin_speed(j: WheelJoint) -> f32 = "k_phys_wheel_spin_speed";
extern fn spin_torque(j: WheelJoint) -> f32 = "k_phys_wheel_spin_torque";
extern fn enable_steering(j: WheelJoint, flag: bool) = "k_phys_wheel_enable_steering";
extern fn steering_enabled(j: WheelJoint) -> bool = "k_phys_wheel_steering_enabled";
extern fn set_steering_hertz(j: WheelJoint, hertz: f32) = "k_phys_wheel_set_steering_hertz";
extern fn steering_hertz(j: WheelJoint) -> f32 = "k_phys_wheel_steering_hertz";
extern fn set_steering_damping_ratio(j: WheelJoint,
                                                                                ratio: f32) = "k_phys_wheel_set_steering_damping_ratio";
extern fn steering_damping_ratio(j: WheelJoint) -> f32 = "k_phys_wheel_steering_damping_ratio";
extern fn set_max_steering_torque(j: WheelJoint,
                                                                          torque: f32) = "k_phys_wheel_set_max_steering_torque";
extern fn max_steering_torque(j: WheelJoint) -> f32 = "k_phys_wheel_max_steering_torque";
extern fn enable_steering_limit(j: WheelJoint, flag: bool) = "k_phys_wheel_enable_steering_limit";
extern fn steering_limit_enabled(j: WheelJoint) -> bool = "k_phys_wheel_steering_limit_enabled";
extern fn lower_steering_limit(j: WheelJoint) -> f32 = "k_phys_wheel_lower_steering_limit";
extern fn upper_steering_limit(j: WheelJoint) -> f32 = "k_phys_wheel_upper_steering_limit";
extern fn set_steering_limits(j: WheelJoint, lower: f32,
                                                                  upper: f32) = "k_phys_wheel_set_steering_limits";
extern fn set_target_steering_angle(j: WheelJoint,
                                                                              radians: f32) = "k_phys_wheel_set_target_steering_angle";
extern fn target_steering_angle(j: WheelJoint) -> f32 = "k_phys_wheel_target_steering_angle";
extern fn steering_angle(j: WheelJoint) -> f32 = "k_phys_wheel_steering_angle";
extern fn steering_torque(j: WheelJoint) -> f32 = "k_phys_wheel_steering_torque";

// --- contacts ----------------------------------------------------------------

extern fn is_valid(c: Contact) -> bool = "k_phys_contact_is_valid";
extern fn manifolds_into(c: Contact, out: Manifold[:]) -> i64 = "k_phys_contact_manifolds";
fn manifolds(c: Contact) -> Manifold[>..] {
    var out: Manifold[>..] = [];
    let n = manifolds_into(c, out);
    out.resize(n, default<Manifold>());
    manifolds_into(c, out);
    return out;
}

// --- hulls -------------------------------------------------------------------

// The convex hull of the points, with at most max_vertices corners (4 to
// MAX_HULL_VERTICES); a zero handle for points in a plane or fewer than four.
extern fn create_hull(points: const float3[:], max_vertices: i64) -> Hull = "k_phys_create_hull";
fn create_hull(points: const float3[:]) -> Hull { create_hull(points, MAX_HULL_VERTICES) }
extern fn create_box_hull(half_extents: float3, frame: Transform) -> Hull = "k_phys_create_box_hull";
fn create_box_hull(half_extents: float3) -> Hull {
    create_box_hull(half_extents, transform_identity)
}
// Along y, centered at y_offset, with 3 to 32 sides.
extern fn create_cylinder_hull(height: f32, radius: f32,
                                                              y_offset: f32, sides: i64) -> Hull = "k_phys_create_cylinder_hull";
// A cone cut off: radius1 at the bottom, radius2 at the top, 4 to 32 slices.
extern fn create_cone_hull(height: f32, radius1: f32, radius2: f32,
                                                      slices: i64) -> Hull = "k_phys_create_cone_hull";
extern fn create_rock_hull(radius: f32) -> Hull = "k_phys_create_rock_hull";
// A new hull: this one scaled, then transformed.
extern fn transformed(h: Hull, transform: Transform,
                                                 scale: float3) -> Hull = "k_phys_hull_transformed";
extern fn destroy(h: Hull) = "k_phys_hull_destroy";
extern fn is_valid(h: Hull) -> bool = "k_phys_hull_is_valid";
extern fn info(h: Hull) -> HullInfo = "k_phys_hull_info_of";
extern fn vertices_into(h: Hull, out: float3[:]) -> i64 = "k_phys_hull_vertices";
// Each face fanned into triangles of vertex indices, wound counterclockwise
// seen from outside.
extern fn triangles_into(h: Hull, out: int3[:]) -> i64 = "k_phys_hull_triangles";
fn vertices(h: Hull) -> float3[>..] {
    var out: float3[>..] = [];
    let n = vertices_into(h, out);
    out.resize(n, float3_0);
    vertices_into(h, out);
    return out;
}
fn triangles(h: Hull) -> int3[>..] {
    var out: int3[>..] = [];
    let n = triangles_into(h, out);
    out.resize(n, int3_0);
    triangles_into(h, out);
    return out;
}

// --- meshes ------------------------------------------------------------------

// Triangles as index triples into vertices; material_indices, if any, one
// per triangle.
extern fn create_mesh(vertices: const float3[:], indices: const i32[:],
                                            material_indices: const u8[:], def: MeshDef) -> Mesh = "k_phys_create_mesh";
fn create_mesh(vertices: const float3[:], indices: const i32[:]) -> Mesh {
    let none: u8[>..] = [];
    create_mesh(vertices, indices, none, MeshDef {})
}
// A flat grid in the xz plane, x_count by z_count cells.
extern fn create_grid_mesh(x_count: i64, z_count: i64, cell_width: f32,
                                                      material_count: i64,
                                                      identify_edges: bool) -> Mesh = "k_phys_create_grid_mesh";
extern fn create_wave_mesh(x_count: i64, z_count: i64, cell_width: f32,
                                                      amplitude: f32, row_frequency: f32,
                                                      column_frequency: f32) -> Mesh = "k_phys_create_wave_mesh";
extern fn create_torus_mesh(radial_resolution: i64,
                                                        tubular_resolution: i64, radius: f32,
                                                        thickness: f32) -> Mesh = "k_phys_create_torus_mesh";
extern fn create_box_mesh(center: float3, extent: float3,
                                                    identify_edges: bool) -> Mesh = "k_phys_create_box_mesh";
// A box open inside, to hold things.
extern fn create_hollow_box_mesh(center: float3,
                                                                  extent: float3) -> Mesh = "k_phys_create_hollow_box_mesh";
extern fn create_platform_mesh(center: float3, height: f32,
                                                              top_width: f32,
                                                              bottom_width: f32) -> Mesh = "k_phys_create_platform_mesh";
extern fn destroy(m: Mesh) = "k_phys_mesh_destroy";
extern fn is_valid(m: Mesh) -> bool = "k_phys_mesh_is_valid";
extern fn info(m: Mesh) -> MeshInfo = "k_phys_mesh_info_of";
extern fn vertices_into(m: Mesh, out: float3[:]) -> i64 = "k_phys_mesh_vertices";
extern fn triangles_into(m: Mesh, out: int3[:]) -> i64 = "k_phys_mesh_triangles";
fn vertices(m: Mesh) -> float3[>..] {
    var out: float3[>..] = [];
    let n = vertices_into(m, out);
    out.resize(n, float3_0);
    vertices_into(m, out);
    return out;
}
fn triangles(m: Mesh) -> int3[>..] {
    var out: int3[>..] = [];
    let n = triangles_into(m, out);
    out.resize(n, int3_0);
    triangles_into(m, out);
    return out;
}

// --- height fields -----------------------------------------------------------

// material_indices, if any, one per cell ((count_x - 1) * (count_z - 1)):
// HEIGHT_FIELD_HOLE for a hole.
extern fn create_height_field(heights: const f32[:],
                                                            material_indices: const u8[:],
                                                            def: HeightFieldDef) -> HeightField = "k_phys_create_height_field";
extern fn create_grid_height_field(
    rows: i64, columns: i64, scale: float3, make_holes: bool) -> HeightField = "k_phys_create_grid_height_field";
extern fn create_wave_height_field(
    rows: i64, columns: i64, scale: float3, row_frequency: f32, column_frequency: f32,
    make_holes: bool) -> HeightField = "k_phys_create_wave_height_field";
extern fn destroy(h: HeightField) = "k_phys_height_field_destroy";
extern fn is_valid(h: HeightField) -> bool = "k_phys_height_field_is_valid";
extern fn info(h: HeightField) -> HeightFieldInfo = "k_phys_height_field_info_of";

// --- baked compounds ---------------------------------------------------------

// Many children in one shape of a static body, with a tree of their own.
extern fn create_compound(spheres: const CompoundSphere[:],
                                                    capsules: const CompoundCapsule[:],
                                                    hulls: const CompoundHull[:],
                                                    meshes: const CompoundMesh[:]) -> Compound = "k_phys_create_compound";
extern fn destroy(c: Compound) = "k_phys_compound_destroy";
extern fn is_valid(c: Compound) -> bool = "k_phys_compound_is_valid";
extern fn info(c: Compound) -> CompoundInfo = "k_phys_compound_info_of";

// --- geometry on its own -----------------------------------------------------

extern fn compute_mass(s: Sphere, density: f32) -> MassData = "k_phys_sphere_mass";
extern fn compute_mass(c: Capsule, density: f32) -> MassData = "k_phys_capsule_mass";
extern fn compute_mass(h: Hull, density: f32) -> MassData = "k_phys_hull_mass";
extern fn compute_aabb(s: Sphere, transform: Transform) -> AABB = "k_phys_sphere_aabb";
extern fn compute_aabb(c: Capsule, transform: Transform) -> AABB = "k_phys_capsule_aabb";
extern fn compute_aabb(h: Hull, transform: Transform) -> AABB = "k_phys_hull_aabb";
extern fn compute_aabb(m: Mesh, transform: Transform, scale: float3) -> AABB = "k_phys_mesh_aabb";
extern fn compute_aabb(h: HeightField, transform: Transform) -> AABB = "k_phys_height_field_aabb";
extern fn compute_aabb(c: Compound, transform: Transform) -> AABB = "k_phys_compound_aabb";

// A ray in the geometry's frame, to max_fraction of the translation.
extern fn ray_cast(s: Sphere, origin: float3, translation: float3,
                                             max_fraction: f32) -> CastOutput = "k_phys_sphere_ray_cast";
extern fn ray_cast(c: Capsule, origin: float3, translation: float3,
                                              max_fraction: f32) -> CastOutput = "k_phys_capsule_ray_cast";
extern fn ray_cast(h: Hull, origin: float3, translation: float3,
                                           max_fraction: f32) -> CastOutput = "k_phys_hull_ray_cast";
extern fn ray_cast(m: Mesh, scale: float3, origin: float3,
                                           translation: float3, max_fraction: f32) -> CastOutput = "k_phys_mesh_ray_cast";
extern fn ray_cast(h: HeightField, origin: float3,
                                                   translation: float3,
                                                   max_fraction: f32) -> CastOutput = "k_phys_height_field_ray_cast";
extern fn ray_cast(c: Compound, origin: float3, translation: float3,
                                               max_fraction: f32) -> CastOutput = "k_phys_compound_ray_cast";

// Whether the geometry, placed by transform, overlaps the convex hull of
// the points grown by radius.
extern fn overlap(s: Sphere, transform: Transform, points: const float3[:],
                                           radius: f32) -> bool = "k_phys_sphere_overlap";
extern fn overlap(c: Capsule, transform: Transform,
                                            points: const float3[:], radius: f32) -> bool = "k_phys_capsule_overlap";
extern fn overlap(h: Hull, transform: Transform, points: const float3[:],
                                         radius: f32) -> bool = "k_phys_hull_overlap";
extern fn overlap(m: Mesh, scale: float3, transform: Transform,
                                         points: const float3[:], radius: f32) -> bool = "k_phys_mesh_overlap";
extern fn overlap(h: HeightField, transform: Transform,
                                                 points: const float3[:], radius: f32) -> bool = "k_phys_height_field_overlap";
extern fn overlap(c: Compound, transform: Transform,
                                             points: const float3[:], radius: f32) -> bool = "k_phys_compound_overlap";

// The points grown by radius, swept along translation against the geometry,
// in its frame.
extern fn shape_cast(s: Sphere, points: const float3[:], radius: f32,
                                                 translation: float3, max_fraction: f32,
                                                 can_encroach: bool) -> CastOutput = "k_phys_sphere_shape_cast";
extern fn shape_cast(c: Capsule, points: const float3[:], radius: f32,
                                                  translation: float3, max_fraction: f32,
                                                  can_encroach: bool) -> CastOutput = "k_phys_capsule_shape_cast";
extern fn shape_cast(h: Hull, points: const float3[:], radius: f32,
                                               translation: float3, max_fraction: f32,
                                               can_encroach: bool) -> CastOutput = "k_phys_hull_shape_cast";
extern fn shape_cast(m: Mesh, scale: float3, points: const float3[:],
                                               radius: f32, translation: float3, max_fraction: f32,
                                               can_encroach: bool) -> CastOutput = "k_phys_mesh_shape_cast";
extern fn shape_cast(h: HeightField, points: const float3[:],
                                                       radius: f32, translation: float3,
                                                       max_fraction: f32,
                                                       can_encroach: bool) -> CastOutput = "k_phys_height_field_shape_cast";
extern fn shape_cast(c: Compound, points: const float3[:],
                                                   radius: f32, translation: float3,
                                                   max_fraction: f32,
                                                   can_encroach: bool) -> CastOutput = "k_phys_compound_shape_cast";

// Between two point clouds with radii, b placed by b_in_a in a's frame.
extern fn shape_distance(points_a: const float3[:], radius_a: f32,
                                                  points_b: const float3[:], radius_b: f32,
                                                  b_in_a: Transform,
                                                  use_radii: bool) -> DistanceOutput = "k_phys_shape_distance";
extern fn shape_cast(points_a: const float3[:], radius_a: f32,
                                               points_b: const float3[:], radius_b: f32,
                                               b_in_a: Transform, translation_b: float3,
                                               max_fraction: f32, can_encroach: bool) -> CastOutput = "k_phys_shape_cast_pair";
// When two moving point clouds first touch, over their sweeps.
extern fn time_of_impact(points_a: const float3[:], radius_a: f32,
                                                  sweep_a: Sweep, points_b: const float3[:],
                                                  radius_b: f32, sweep_b: Sweep,
                                                  max_fraction: f32) -> ToiOutput = "k_phys_time_of_impact";
extern fn sweep_transform(sweep: Sweep, time: f32) -> Transform = "k_phys_sweep_transform";

// The character mover's solver: the move closest to target_delta that the
// planes allow, pushing out of them within their limits; each plane's
// `push` is updated. Also how many iterations it took.
extern fn solve_planes_into(target_delta: float3, planes: CollisionPlane[:],
                                                   delta: float3&) -> i64 = "k_phys_solve_planes";
fn solve_planes(target_delta: float3, planes: CollisionPlane[:]) -> float3, i64 {
    var delta = float3_0;
    let n = solve_planes_into(target_delta, planes, delta);
    return delta, n;
}
// A velocity with what points into the planes removed.
extern fn clip_vector(vector: float3,
                                            planes: const CollisionPlane[:]) -> float3 = "k_phys_clip_vector";

// --- rotations and transforms ------------------------------------------------

// A rotation of `angle` radians about `axis`, counterclockwise looking down
// the axis.
fn quat_axis_angle(axis: float3, angle: f32) -> Quat {
    let a = ::normalize(axis);
    let s = sin(angle * 0.5);
    Quat { a.x * s, a.y * s, a.z * s, cos(angle * 0.5) }
}

fn normalize(q: Quat) -> Quat {
    let l = sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    Quat { q.x / l, q.y / l, q.z / l, q.w / l }
}

fn conjugate(q: Quat) -> Quat { Quat { -q.x, -q.y, -q.z, q.w } }

// The rotation b, then a.
fn mul(a: Quat, b: Quat) -> Quat {
    Quat { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
           a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
           a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
           a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z }
}

fn rotate(q: Quat, v: float3) -> float3 {
    let u = float3 { q.x, q.y, q.z };
    let t = cross(u, v) * float3 { 2.0, 2.0, 2.0 };
    v + t * float3 { q.w, q.w, q.w } + cross(u, t)
}

fn inv_rotate(q: Quat, v: float3) -> float3 { rotate(conjugate(q), v) }

// The rotation as the columns of a matrix, for drawing.
fn rotation_matrix(q: Quat) -> Mat3 {
    Mat3 { cx: rotate(q, float3 { 1.0, 0.0, 0.0 }), cy: rotate(q, float3 { 0.0, 1.0, 0.0 }),
           cz: rotate(q, float3 { 0.0, 0.0, 1.0 }) }
}

fn transform_point(t: Transform, p: float3) -> float3 { rotate(t.q, p) + t.p }
fn inv_transform_point(t: Transform, p: float3) -> float3 { inv_rotate(t.q, p - t.p) }

// The transform b, then a: from b's frame to a's parent.
fn mul(a: Transform, b: Transform) -> Transform {
    Transform { p: transform_point(a, b.p), q: mul(a.q, b.q) }
}

// b seen from a's frame.
fn inv_mul(a: Transform, b: Transform) -> Transform {
    Transform { p: inv_rotate(a.q, b.p - a.p), q: mul(conjugate(a.q), b.q) }
}

// A frame in the world as the body sees it, for joint definitions.
fn local_frame(b: Body, world_frame: Transform) -> Transform { inv_mul(transform(b), world_frame) }

// A joint definition's common part: bodies a and b, meeting at `frame`, a
// frame in the world.
fn joint_def(a: Body, b: Body, frame: Transform) -> JointDef {
    JointDef { body_a: a, body_b: b, frame_a: local_frame(a, frame),
               frame_b: local_frame(b, frame) }
}
// Meeting at a point in the world, with the world's axes.
fn joint_def(a: Body, b: Body, anchor: float3) -> JointDef {
    joint_def(a, b, Transform { p: anchor, q: quat_identity })
}
