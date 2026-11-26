// Adapted for K.
/* The physics layer's C API: every function physics.k reaches through
   an `extern "k_phys_..." fn`, listed once in K_PHYS_API. That list
   expands into the prototypes below, and the test suite checks
   physics.k against it (test/api_check.py).

   The layer sits over Box3D (third_party/box3d) and changes little of it:
   the same worlds, bodies, shapes, joints and queries, under the same
   names. What it does change is what cannot cross an `extern fn` (spec
   7.10): Box3D's definitions carry pointers, names and callbacks, and its
   structs are naturally aligned where K's are packed, so every struct
   here is K-shaped and packed, and converted field by field. Callbacks
   become arrays: a query or an event list is copied into a slice the
   program provides, and returns how many there were, so a second call with
   a larger slice gets the rest. Friction and restitution mixing, which Box3D
   takes as callbacks, are picked from a fixed set of rules instead.

   What crosses by value is kept to what TinyCC passes the way the C
   compilers do: scalars, all-integer or all-float structs of up to 16 bytes,
   and structs larger than 16 bytes. TinyCC classifies a small struct as a
   whole, where the System V ABI classifies each eightbyte, so a small struct
   mixing floats and integers would arrive in the wrong registers; the API
   check rejects one.

   Handles are Box3D's own ids, stored as integers: a world in a u32, bodies,
   shapes and joints in a u64, each carrying a generation, so a destroyed
   object is an error to use, not a crash. Every call checks its handles
   before Box3D sees them. Geometry the layer owns (hulls, meshes, height
   fields, compounds, recordings, players) lives in slot tables with
   generations of their own.

   Errors: a function that can fail for reasons outside the program (a file
   that is not there, a degenerate hull) returns false or a zero handle, with
   the reason in k_phys_error. A call the program should not have made (a
   destroyed body, a revolute function on a prismatic joint, a static body
   given a mass) is skipped and counted by k_phys_misuse_count, which the
   K side checks at every step and turns into an abort. The error text
   and the misuse count are per thread; one world must only be used by one
   thread at a time. */

#ifndef K_PHYS_API_H
#define K_PHYS_API_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#pragma pack(push, 1)

/* --- slices, vectors and handles ------------------------------------------ */

typedef struct { uint8_t *data; int64_t len; } k_phys_bytes;   /* u8[:], the generated sl_u8 */

typedef struct { float x, y; } k_phys_float2;
typedef struct { float x, y, z; } k_phys_float3;
typedef struct { int32_t x, y, z; } k_phys_int3;

typedef struct { uint32_t id; } k_phys_world;
typedef struct { uint64_t id; } k_phys_body;
typedef struct { uint64_t id; } k_phys_shape;
typedef struct { uint64_t id; } k_phys_joint;
typedef struct { int32_t index1; uint16_t world0; int16_t reserved; uint32_t generation; } k_phys_contact;

/* A joint of one kind: the functions only that kind has take these. */
typedef struct { k_phys_joint joint; } k_phys_distance_joint;
typedef struct { k_phys_joint joint; } k_phys_filter_joint;
typedef struct { k_phys_joint joint; } k_phys_motor_joint;
typedef struct { k_phys_joint joint; } k_phys_parallel_joint;
typedef struct { k_phys_joint joint; } k_phys_prismatic_joint;
typedef struct { k_phys_joint joint; } k_phys_revolute_joint;
typedef struct { k_phys_joint joint; } k_phys_spherical_joint;
typedef struct { k_phys_joint joint; } k_phys_weld_joint;
typedef struct { k_phys_joint joint; } k_phys_wheel_joint;

/* Geometry and recordings, owned by the layer. */
typedef struct { uint32_t id; } k_phys_hull;
typedef struct { uint32_t id; } k_phys_mesh;
typedef struct { uint32_t id; } k_phys_height_field;
typedef struct { uint32_t id; } k_phys_compound;
typedef struct { uint32_t id; } k_phys_recording;
typedef struct { uint32_t id; } k_phys_player;

/* --- math ----------------------------------------------------------------- */

typedef struct { float x, y, z, w; } k_phys_quat;        /* b3Quat: v = (x, y, z), s = w */
typedef struct { k_phys_float3 p; k_phys_quat q; } k_phys_transform;
typedef struct { k_phys_float3 cx, cy, cz; } k_phys_mat3;
typedef struct { k_phys_float3 lower, upper; } k_phys_aabb;
typedef struct { k_phys_float3 normal; float offset; } k_phys_plane;
typedef struct { float mass; k_phys_float3 center; k_phys_mat3 inertia; } k_phys_mass_data;
typedef struct { k_phys_float3 center; float radius; } k_phys_sphere;
typedef struct { k_phys_float3 center1, center2; float radius; } k_phys_capsule;
typedef struct { int32_t major, minor, revision; } k_phys_version;

/* --- definitions: Box3D's b3*Def without pointers ------------------------- */

typedef struct {
    int32_t static_shapes, dynamic_shapes, static_bodies, dynamic_bodies, contacts;
} k_phys_capacity;

typedef struct {
    k_phys_float3 gravity;
    float restitution_threshold, hit_event_threshold;
    float contact_hertz, contact_damping_ratio, contact_speed;
    float maximum_linear_speed;
    uint8_t enable_sleep, enable_continuous;
    int32_t workers;                /* 1: serial; more: Box3D's own worker threads */
    int32_t friction_mixing, restitution_mixing;    /* a MIX_* rule */
    uint64_t user_data;
    k_phys_capacity capacity;
} k_phys_world_def;

typedef struct {
    uint8_t linear_x, linear_y, linear_z, angular_x, angular_y, angular_z;
} k_phys_motion_locks;

typedef struct {
    int32_t body_type;
    k_phys_float3 position;
    k_phys_quat rotation;
    k_phys_float3 linear_velocity, angular_velocity;
    float linear_damping, angular_damping, gravity_scale;
    float sleep_threshold, safety_factor;
    k_phys_motion_locks motion_locks;
    uint8_t enable_sleep, is_awake, is_bullet, is_enabled, allow_fast_rotation;
    uint8_t enable_contact_recycling;
    uint64_t user_data;
} k_phys_body_def;

typedef struct {
    uint64_t category_bits, mask_bits;
    int32_t group_index;
} k_phys_filter;

typedef struct { uint64_t category_bits, mask_bits; } k_phys_query_filter;

typedef struct {
    float friction, restitution, rolling_resistance;
    k_phys_float3 tangent_velocity;
    uint64_t user_material_id;
    uint32_t custom_color;
} k_phys_surface_material;

typedef struct {
    k_phys_surface_material material;
    float density;
    k_phys_filter filter;
    uint8_t is_sensor, enable_sensor_events, enable_contact_events, enable_hit_events;
    uint8_t invoke_contact_creation, update_body_mass, enable_speculative_contact;
    float explosion_scale;
    uint64_t user_data;
} k_phys_shape_def;

typedef struct {
    k_phys_float3 position;
    float radius, falloff, impulse_per_area;
    uint64_t mask_bits;
} k_phys_explosion_def;

/* What every joint definition starts with, b3JointDef. */
typedef struct {
    k_phys_body body_a, body_b;
    k_phys_transform frame_a, frame_b;     /* the joint frame in each body */
    uint8_t collide_connected;
    float force_threshold, torque_threshold;
    float constraint_hertz, constraint_damping_ratio;
    float draw_scale;
    uint64_t user_data;
} k_phys_joint_def;

typedef struct {
    k_phys_joint_def base;
    float length;
    uint8_t enable_spring;
    float lower_spring_force, upper_spring_force, hertz, damping_ratio;
    uint8_t enable_limit;
    float min_length, max_length;
    uint8_t enable_motor;
    float max_motor_force, motor_speed;
} k_phys_distance_joint_def;

typedef struct { k_phys_joint_def base; } k_phys_filter_joint_def;

typedef struct {
    k_phys_joint_def base;
    k_phys_float3 linear_velocity;
    float max_velocity_force;
    k_phys_float3 angular_velocity;
    float max_velocity_torque;
    float linear_hertz, linear_damping_ratio, max_spring_force;
    float angular_hertz, angular_damping_ratio, max_spring_torque;
} k_phys_motor_joint_def;

typedef struct {
    k_phys_joint_def base;
    float hertz, damping_ratio, max_torque;
} k_phys_parallel_joint_def;

typedef struct {
    k_phys_joint_def base;
    uint8_t enable_spring;
    float hertz, damping_ratio, target_translation;
    uint8_t enable_limit;
    float lower_translation, upper_translation;
    uint8_t enable_motor;
    float max_motor_force, motor_speed;
} k_phys_prismatic_joint_def;

typedef struct {
    k_phys_joint_def base;
    float target_angle;
    uint8_t enable_spring;
    float hertz, damping_ratio;
    uint8_t enable_limit;
    float lower_angle, upper_angle;
    uint8_t enable_motor;
    float max_motor_torque, motor_speed;
} k_phys_revolute_joint_def;

typedef struct {
    k_phys_joint_def base;
    uint8_t enable_spring;
    float hertz, damping_ratio;
    k_phys_quat target_rotation;
    uint8_t enable_cone_limit;
    float cone_angle;
    uint8_t enable_twist_limit;
    float lower_twist_angle, upper_twist_angle;
    uint8_t enable_motor;
    float max_motor_torque;
    k_phys_float3 motor_velocity;
} k_phys_spherical_joint_def;

typedef struct {
    k_phys_joint_def base;
    float linear_hertz, angular_hertz, linear_damping_ratio, angular_damping_ratio;
} k_phys_weld_joint_def;

typedef struct {
    k_phys_joint_def base;
    uint8_t enable_suspension_spring;
    float suspension_hertz, suspension_damping_ratio;
    uint8_t enable_suspension_limit;
    float lower_suspension_limit, upper_suspension_limit;
    uint8_t enable_spin_motor;
    float max_spin_torque, spin_speed;
    uint8_t enable_steering;
    float steering_hertz, steering_damping_ratio, target_steering_angle, max_steering_torque;
    uint8_t enable_steering_limit;
    float lower_steering_limit, upper_steering_limit;
} k_phys_wheel_joint_def;

/* --- events, queries and contacts ----------------------------------------- */

typedef struct {
    k_phys_transform transform;
    k_phys_body body;
    uint64_t user_data;
    uint8_t fell_asleep;
} k_phys_body_move_event;

typedef struct { k_phys_shape sensor, visitor; } k_phys_sensor_event;

typedef struct {
    k_phys_shape shape_a, shape_b;
    k_phys_contact contact;
} k_phys_contact_event;

typedef struct {
    k_phys_shape shape_a, shape_b;
    k_phys_contact contact;
    k_phys_float3 point, normal;
    float approach_speed;
    uint64_t material_a, material_b;
} k_phys_contact_hit_event;

typedef struct {
    k_phys_joint joint;
    uint64_t user_data;
} k_phys_joint_event;

/* A ray or shape cast's hit against the world or a body. */
typedef struct {
    k_phys_shape shape;
    k_phys_float3 point, normal;
    float fraction;
    uint64_t material_id;
    int32_t triangle_index, child_index;
    uint8_t hit;
} k_phys_ray_hit;

/* A cast against one piece of geometry, in its own frame. */
typedef struct {
    k_phys_float3 normal, point;
    float fraction;
    int32_t iterations, triangle_index, child_index, material_index;
    uint8_t hit;
} k_phys_cast_output;

/* A plane a character mover touches, b3PlaneResult with its shape. */
typedef struct {
    k_phys_shape shape;
    k_phys_plane plane;
    k_phys_float3 point;
    int32_t triangle_index, child_index, material_index;
} k_phys_plane_hit;

typedef struct {
    k_phys_plane plane;
    float push_limit, push;
    uint8_t clip_velocity;
} k_phys_collision_plane;

typedef struct {
    k_phys_float3 point, normal;
    float fraction;
    k_phys_shape shape;
} k_phys_toi_hit;

typedef struct {
    k_phys_float3 point_a, point_b, normal;
    float distance;
    int32_t iterations;
} k_phys_distance_output;

typedef struct {
    k_phys_float3 local_center, c1, c2;
    k_phys_quat q1, q2;
} k_phys_sweep;

typedef struct {
    int32_t state;
    k_phys_float3 point, normal;
    float fraction, distance;
} k_phys_toi_output;

typedef struct {
    k_phys_float3 anchor_a, anchor_b;
    float separation, base_separation;
    float normal_impulse, total_normal_impulse, normal_velocity;
    uint32_t feature_id;
    int32_t triangle_index;
    uint8_t persisted;
} k_phys_manifold_point;

/* One manifold of one contact, with the contact it belongs to. */
typedef struct {
    k_phys_contact contact;
    k_phys_shape shape_a, shape_b;
    k_phys_float3 normal;
    float twist_impulse;
    k_phys_float3 friction_impulse, rolling_impulse;
    int32_t point_count;
    k_phys_manifold_point points[4];
} k_phys_manifold;

typedef struct {
    float step, pairs, collide, solve, solver_setup, constraints, prepare_constraints;
    float integrate_velocities, warm_start, solve_impulses, integrate_positions;
    float relax_impulses, apply_restitution, store_impulses, split_islands, transforms;
    float sensor_hits, joint_events, hit_events, refit, bullets, sleep_islands, sensors;
} k_phys_profile;

typedef struct {
    int32_t body_count, shape_count, contact_count, joint_count, island_count;
    int32_t stack_used, arena_capacity, static_tree_height, tree_height;
    int32_t sat_call_count, sat_cache_hit_count, byte_count, task_count;
    int32_t color_counts[24];
    int32_t manifold_counts[8];
    int32_t awake_contact_count, recycled_contact_count;
    int32_t distance_iterations, push_back_iterations, root_iterations;
} k_phys_counters;

/* --- geometry ------------------------------------------------------------- */

typedef struct {
    int32_t vertex_count, half_edge_count, face_count, byte_count;
    float volume, surface_area, inner_radius;
    k_phys_float3 center;
    k_phys_mat3 central_inertia;
    k_phys_aabb aabb;
} k_phys_hull_info;

typedef struct {
    float weld_tolerance;
    uint8_t weld_vertices, use_median_split, identify_edges, clockwise;
} k_phys_mesh_def;

typedef struct {
    int32_t vertex_count, triangle_count, material_count, degenerate_count;
    int32_t node_count, tree_height, byte_count;
    float surface_area;
    k_phys_aabb bounds;
} k_phys_mesh_info;

typedef struct {
    k_phys_float3 scale;
    int32_t count_x, count_z;
    float min_height, max_height;   /* both 0: from the heights */
    uint8_t clockwise;
} k_phys_height_field_def;

typedef struct {
    int32_t columns, rows, byte_count;
    float min_height, max_height;
    k_phys_float3 scale;
    k_phys_aabb aabb;
} k_phys_height_field_info;

typedef struct { k_phys_sphere sphere; k_phys_surface_material material; } k_phys_compound_sphere;
typedef struct { k_phys_capsule capsule; k_phys_surface_material material; } k_phys_compound_capsule;
typedef struct {
    k_phys_hull hull;
    k_phys_transform transform;
    k_phys_surface_material material;
} k_phys_compound_hull;
typedef struct {
    k_phys_mesh mesh;
    k_phys_transform transform;
    k_phys_float3 scale;
    k_phys_surface_material material;  /* for each of the mesh's materials */
} k_phys_compound_mesh;

typedef struct {
    int32_t sphere_count, capsule_count, hull_count, mesh_count, material_count;
    int32_t byte_count;
} k_phys_compound_info;

typedef struct {
    int32_t frame_count, worker_count;
    float time_step;
    int32_t sub_step_count;
    float length_scale;
    k_phys_aabb bounds;
} k_phys_player_info;

/* Slices of the elements above, as the generated C passes them. */
typedef struct { k_phys_float3 *data; int64_t len; } k_phys_float3_slice;
typedef struct { k_phys_int3 *data; int64_t len; } k_phys_int3_slice;
typedef struct { int32_t *data; int64_t len; } k_phys_i32_slice;
typedef struct { float *data; int64_t len; } k_phys_f32_slice;
typedef struct { k_phys_body *data; int64_t len; } k_phys_body_slice;
typedef struct { k_phys_shape *data; int64_t len; } k_phys_shape_slice;
typedef struct { k_phys_joint *data; int64_t len; } k_phys_joint_slice;
typedef struct { k_phys_transform *data; int64_t len; } k_phys_transform_slice;
typedef struct { k_phys_surface_material *data; int64_t len; } k_phys_surface_material_slice;
typedef struct { k_phys_body_move_event *data; int64_t len; } k_phys_body_move_event_slice;
typedef struct { k_phys_sensor_event *data; int64_t len; } k_phys_sensor_event_slice;
typedef struct { k_phys_contact_event *data; int64_t len; } k_phys_contact_event_slice;
typedef struct { k_phys_contact_hit_event *data; int64_t len; } k_phys_contact_hit_event_slice;
typedef struct { k_phys_joint_event *data; int64_t len; } k_phys_joint_event_slice;
typedef struct { k_phys_ray_hit *data; int64_t len; } k_phys_ray_hit_slice;
typedef struct { k_phys_plane_hit *data; int64_t len; } k_phys_plane_hit_slice;
typedef struct { k_phys_collision_plane *data; int64_t len; } k_phys_collision_plane_slice;
typedef struct { k_phys_manifold *data; int64_t len; } k_phys_manifold_slice;
typedef struct { k_phys_compound_sphere *data; int64_t len; } k_phys_compound_sphere_slice;
typedef struct { k_phys_compound_capsule *data; int64_t len; } k_phys_compound_capsule_slice;
typedef struct { k_phys_compound_hull *data; int64_t len; } k_phys_compound_hull_slice;
typedef struct { k_phys_compound_mesh *data; int64_t len; } k_phys_compound_mesh_slice;

#pragma pack(pop)

#define K_PHYS_API(X) \
    /* The library. */ \
    X(uint8_t, k_phys_available, (void)) \
    X(int64_t, k_phys_error, (k_phys_bytes out)) \
    X(int64_t, k_phys_misuse_count, (void)) \
    X(k_phys_version, k_phys_version_of, (void)) \
    X(int64_t, k_phys_byte_count, (void)) \
    X(void, k_phys_set_length_units_per_meter, (float units)) \
    X(float, k_phys_length_units_per_meter, (void)) \
    X(int64_t, k_phys_world_count, (void)) \
    X(int64_t, k_phys_max_world_count, (void)) \
    /* Box3D's defaults, which the K field defaults repeat. */ \
    X(k_phys_world_def, k_phys_default_world_def, (void)) \
    X(k_phys_body_def, k_phys_default_body_def, (void)) \
    X(k_phys_shape_def, k_phys_default_shape_def, (void)) \
    X(k_phys_surface_material, k_phys_default_surface_material, (void)) \
    X(k_phys_filter, k_phys_default_filter, (void)) \
    X(k_phys_query_filter, k_phys_default_query_filter, (void)) \
    X(k_phys_explosion_def, k_phys_default_explosion_def, (void)) \
    X(k_phys_distance_joint_def, k_phys_default_distance_joint_def, (void)) \
    X(k_phys_filter_joint_def, k_phys_default_filter_joint_def, (void)) \
    X(k_phys_motor_joint_def, k_phys_default_motor_joint_def, (void)) \
    X(k_phys_parallel_joint_def, k_phys_default_parallel_joint_def, (void)) \
    X(k_phys_prismatic_joint_def, k_phys_default_prismatic_joint_def, (void)) \
    X(k_phys_revolute_joint_def, k_phys_default_revolute_joint_def, (void)) \
    X(k_phys_spherical_joint_def, k_phys_default_spherical_joint_def, (void)) \
    X(k_phys_weld_joint_def, k_phys_default_weld_joint_def, (void)) \
    X(k_phys_wheel_joint_def, k_phys_default_wheel_joint_def, (void)) \
    /* Worlds. */ \
    X(k_phys_world, k_phys_create_world, (k_phys_world_def def)) \
    X(void, k_phys_world_destroy, (k_phys_world w)) \
    X(uint8_t, k_phys_world_is_valid, (k_phys_world w)) \
    X(void, k_phys_world_step, (k_phys_world w, float time_step, int64_t sub_steps)) \
    X(k_phys_aabb, k_phys_world_bounds, (k_phys_world w)) \
    X(void, k_phys_world_enable_sleeping, (k_phys_world w, uint8_t flag)) \
    X(uint8_t, k_phys_world_sleeping_enabled, (k_phys_world w)) \
    X(void, k_phys_world_enable_continuous, (k_phys_world w, uint8_t flag)) \
    X(uint8_t, k_phys_world_continuous_enabled, (k_phys_world w)) \
    X(void, k_phys_world_set_restitution_threshold, (k_phys_world w, float value)) \
    X(float, k_phys_world_restitution_threshold, (k_phys_world w)) \
    X(void, k_phys_world_set_hit_event_threshold, (k_phys_world w, float value)) \
    X(float, k_phys_world_hit_event_threshold, (k_phys_world w)) \
    X(void, k_phys_world_set_gravity, (k_phys_world w, k_phys_float3 gravity)) \
    X(k_phys_float3, k_phys_world_gravity, (k_phys_world w)) \
    X(void, k_phys_world_explode, (k_phys_world w, k_phys_explosion_def def)) \
    X(void, k_phys_world_set_contact_tuning, (k_phys_world w, float hertz, float damping_ratio, float push_speed)) \
    X(void, k_phys_world_set_contact_recycle_distance, (k_phys_world w, float distance)) \
    X(float, k_phys_world_contact_recycle_distance, (k_phys_world w)) \
    X(void, k_phys_world_set_maximum_linear_speed, (k_phys_world w, float speed)) \
    X(float, k_phys_world_maximum_linear_speed, (k_phys_world w)) \
    X(void, k_phys_world_enable_warm_starting, (k_phys_world w, uint8_t flag)) \
    X(uint8_t, k_phys_world_warm_starting_enabled, (k_phys_world w)) \
    X(int64_t, k_phys_world_awake_body_count, (k_phys_world w)) \
    X(k_phys_profile, k_phys_world_profile, (k_phys_world w)) \
    X(k_phys_counters, k_phys_world_counters, (k_phys_world w)) \
    X(k_phys_capacity, k_phys_world_max_capacity, (k_phys_world w)) \
    X(void, k_phys_world_set_user_data, (k_phys_world w, uint64_t data)) \
    X(uint64_t, k_phys_world_user_data, (k_phys_world w)) \
    X(void, k_phys_world_set_friction_mixing, (k_phys_world w, int64_t rule)) \
    X(void, k_phys_world_set_restitution_mixing, (k_phys_world w, int64_t rule)) \
    X(void, k_phys_world_set_worker_count, (k_phys_world w, int64_t count)) \
    X(int64_t, k_phys_world_worker_count, (k_phys_world w)) \
    /* Events of the last step. */ \
    X(int64_t, k_phys_world_body_move_events, (k_phys_world w, k_phys_body_move_event_slice out)) \
    X(int64_t, k_phys_world_sensor_begin_events, (k_phys_world w, k_phys_sensor_event_slice out)) \
    X(int64_t, k_phys_world_sensor_end_events, (k_phys_world w, k_phys_sensor_event_slice out)) \
    X(int64_t, k_phys_world_contact_begin_events, (k_phys_world w, k_phys_contact_event_slice out)) \
    X(int64_t, k_phys_world_contact_end_events, (k_phys_world w, k_phys_contact_event_slice out)) \
    X(int64_t, k_phys_world_contact_hit_events, (k_phys_world w, k_phys_contact_hit_event_slice out)) \
    X(int64_t, k_phys_world_joint_events, (k_phys_world w, k_phys_joint_event_slice out)) \
    /* World queries. */ \
    X(int64_t, k_phys_world_overlap_aabb, (k_phys_world w, k_phys_aabb box, k_phys_query_filter filter, k_phys_shape_slice out)) \
    X(int64_t, k_phys_world_overlap_shape, (k_phys_world w, k_phys_float3 origin, k_phys_float3_slice points, float radius, k_phys_query_filter filter, k_phys_shape_slice out)) \
    X(int64_t, k_phys_world_cast_ray, (k_phys_world w, k_phys_float3 origin, k_phys_float3 translation, k_phys_query_filter filter, k_phys_ray_hit_slice out)) \
    X(k_phys_ray_hit, k_phys_world_cast_ray_closest, (k_phys_world w, k_phys_float3 origin, k_phys_float3 translation, k_phys_query_filter filter)) \
    X(int64_t, k_phys_world_cast_shape, (k_phys_world w, k_phys_float3 origin, k_phys_float3_slice points, float radius, k_phys_float3 translation, k_phys_query_filter filter, k_phys_ray_hit_slice out)) \
    X(float, k_phys_world_cast_mover, (k_phys_world w, k_phys_float3 origin, k_phys_capsule mover, k_phys_float3 translation, k_phys_query_filter filter)) \
    X(int64_t, k_phys_world_collide_mover, (k_phys_world w, k_phys_float3 origin, k_phys_capsule mover, k_phys_query_filter filter, k_phys_plane_hit_slice out)) \
    /* Recording and replay. */ \
    X(k_phys_recording, k_phys_create_recording, (int64_t capacity)) \
    X(k_phys_recording, k_phys_load_recording, (k_phys_bytes path)) \
    X(void, k_phys_recording_destroy, (k_phys_recording r)) \
    X(uint8_t, k_phys_recording_is_valid, (k_phys_recording r)) \
    X(int64_t, k_phys_recording_size, (k_phys_recording r)) \
    X(int64_t, k_phys_recording_bytes, (k_phys_recording r, k_phys_bytes out)) \
    X(uint8_t, k_phys_recording_save, (k_phys_recording r, k_phys_bytes path)) \
    X(uint8_t, k_phys_recording_validate, (k_phys_recording r, int64_t workers)) \
    X(void, k_phys_world_start_recording, (k_phys_world w, k_phys_recording r)) \
    X(void, k_phys_world_stop_recording, (k_phys_world w)) \
    X(k_phys_player, k_phys_create_player, (k_phys_recording r, int64_t workers)) \
    X(void, k_phys_player_destroy, (k_phys_player p)) \
    X(uint8_t, k_phys_player_is_valid, (k_phys_player p)) \
    X(uint8_t, k_phys_player_step, (k_phys_player p)) \
    X(void, k_phys_player_sub_step, (k_phys_player p)) \
    X(uint8_t, k_phys_player_at_pre_step, (k_phys_player p)) \
    X(void, k_phys_player_restart, (k_phys_player p)) \
    X(void, k_phys_player_seek, (k_phys_player p, int64_t frame)) \
    X(k_phys_world, k_phys_player_world, (k_phys_player p)) \
    X(int64_t, k_phys_player_frame, (k_phys_player p)) \
    X(int64_t, k_phys_player_frame_count, (k_phys_player p)) \
    X(uint8_t, k_phys_player_at_end, (k_phys_player p)) \
    X(uint8_t, k_phys_player_diverged, (k_phys_player p)) \
    X(int64_t, k_phys_player_diverge_frame, (k_phys_player p)) \
    X(int64_t, k_phys_player_body_count, (k_phys_player p)) \
    X(k_phys_body, k_phys_player_body, (k_phys_player p, int64_t index)) \
    X(k_phys_player_info, k_phys_player_info_of, (k_phys_player p)) \
    X(void, k_phys_player_set_worker_count, (k_phys_player p, int64_t count)) \
    /* Bodies. */ \
    X(k_phys_body, k_phys_create_body, (k_phys_world w, k_phys_body_def def)) \
    X(void, k_phys_body_destroy, (k_phys_body b)) \
    X(uint8_t, k_phys_body_is_valid, (k_phys_body b)) \
    X(int32_t, k_phys_body_type, (k_phys_body b)) \
    X(void, k_phys_body_set_type, (k_phys_body b, int64_t body_type)) \
    X(void, k_phys_body_set_name, (k_phys_body b, k_phys_bytes name)) \
    X(int64_t, k_phys_body_name, (k_phys_body b, k_phys_bytes out)) \
    X(void, k_phys_body_set_user_data, (k_phys_body b, uint64_t data)) \
    X(uint64_t, k_phys_body_user_data, (k_phys_body b)) \
    X(k_phys_float3, k_phys_body_position, (k_phys_body b)) \
    X(k_phys_quat, k_phys_body_rotation, (k_phys_body b)) \
    X(k_phys_transform, k_phys_body_transform, (k_phys_body b)) \
    X(void, k_phys_body_transforms, (k_phys_body_slice bodies, k_phys_transform_slice out)) \
    X(void, k_phys_body_set_transform, (k_phys_body b, k_phys_float3 position, k_phys_quat rotation)) \
    X(k_phys_float3, k_phys_body_local_point, (k_phys_body b, k_phys_float3 world_point)) \
    X(k_phys_float3, k_phys_body_world_point, (k_phys_body b, k_phys_float3 local_point)) \
    X(k_phys_float3, k_phys_body_local_vector, (k_phys_body b, k_phys_float3 world_vector)) \
    X(k_phys_float3, k_phys_body_world_vector, (k_phys_body b, k_phys_float3 local_vector)) \
    X(k_phys_float3, k_phys_body_linear_velocity, (k_phys_body b)) \
    X(k_phys_float3, k_phys_body_angular_velocity, (k_phys_body b)) \
    X(void, k_phys_body_set_linear_velocity, (k_phys_body b, k_phys_float3 velocity)) \
    X(void, k_phys_body_set_angular_velocity, (k_phys_body b, k_phys_float3 velocity)) \
    X(void, k_phys_body_set_target_transform, (k_phys_body b, k_phys_transform target, float time_step, uint8_t wake)) \
    X(k_phys_float3, k_phys_body_local_point_velocity, (k_phys_body b, k_phys_float3 local_point)) \
    X(k_phys_float3, k_phys_body_world_point_velocity, (k_phys_body b, k_phys_float3 world_point)) \
    X(void, k_phys_body_apply_force, (k_phys_body b, k_phys_float3 force, k_phys_float3 point, uint8_t wake)) \
    X(void, k_phys_body_apply_force_to_center, (k_phys_body b, k_phys_float3 force, uint8_t wake)) \
    X(void, k_phys_body_apply_torque, (k_phys_body b, k_phys_float3 torque, uint8_t wake)) \
    X(void, k_phys_body_apply_linear_impulse, (k_phys_body b, k_phys_float3 impulse, k_phys_float3 point, uint8_t wake)) \
    X(void, k_phys_body_apply_linear_impulse_to_center, (k_phys_body b, k_phys_float3 impulse, uint8_t wake)) \
    X(void, k_phys_body_apply_angular_impulse, (k_phys_body b, k_phys_float3 impulse, uint8_t wake)) \
    X(float, k_phys_body_mass, (k_phys_body b)) \
    X(k_phys_mat3, k_phys_body_rotational_inertia, (k_phys_body b)) \
    X(float, k_phys_body_inverse_mass, (k_phys_body b)) \
    X(k_phys_mat3, k_phys_body_world_inverse_inertia, (k_phys_body b)) \
    X(k_phys_float3, k_phys_body_local_center, (k_phys_body b)) \
    X(k_phys_float3, k_phys_body_world_center, (k_phys_body b)) \
    X(void, k_phys_body_set_mass_data, (k_phys_body b, k_phys_mass_data data)) \
    X(k_phys_mass_data, k_phys_body_mass_data, (k_phys_body b)) \
    X(void, k_phys_body_apply_mass_from_shapes, (k_phys_body b)) \
    X(void, k_phys_body_set_linear_damping, (k_phys_body b, float damping)) \
    X(float, k_phys_body_linear_damping, (k_phys_body b)) \
    X(void, k_phys_body_set_angular_damping, (k_phys_body b, float damping)) \
    X(float, k_phys_body_angular_damping, (k_phys_body b)) \
    X(void, k_phys_body_set_gravity_scale, (k_phys_body b, float scale)) \
    X(float, k_phys_body_gravity_scale, (k_phys_body b)) \
    X(uint8_t, k_phys_body_is_awake, (k_phys_body b)) \
    X(void, k_phys_body_set_awake, (k_phys_body b, uint8_t awake)) \
    X(void, k_phys_body_enable_sleep, (k_phys_body b, uint8_t flag)) \
    X(uint8_t, k_phys_body_sleep_enabled, (k_phys_body b)) \
    X(void, k_phys_body_set_sleep_threshold, (k_phys_body b, float threshold)) \
    X(float, k_phys_body_sleep_threshold, (k_phys_body b)) \
    X(void, k_phys_body_set_safety_factor, (k_phys_body b, float factor)) \
    X(float, k_phys_body_safety_factor, (k_phys_body b)) \
    X(uint8_t, k_phys_body_is_enabled, (k_phys_body b)) \
    X(void, k_phys_body_disable, (k_phys_body b)) \
    X(void, k_phys_body_enable, (k_phys_body b)) \
    X(void, k_phys_body_set_motion_locks, (k_phys_body b, k_phys_motion_locks locks)) \
    X(k_phys_motion_locks, k_phys_body_motion_locks, (k_phys_body b)) \
    X(void, k_phys_body_set_bullet, (k_phys_body b, uint8_t flag)) \
    X(uint8_t, k_phys_body_is_bullet, (k_phys_body b)) \
    X(void, k_phys_body_allow_fast_rotation, (k_phys_body b, uint8_t flag)) \
    X(uint8_t, k_phys_body_fast_rotation_allowed, (k_phys_body b)) \
    X(void, k_phys_body_enable_contact_recycling, (k_phys_body b, uint8_t flag)) \
    X(uint8_t, k_phys_body_contact_recycling_enabled, (k_phys_body b)) \
    X(void, k_phys_body_enable_hit_events, (k_phys_body b, uint8_t flag)) \
    X(k_phys_world, k_phys_body_world, (k_phys_body b)) \
    X(int64_t, k_phys_body_shape_count, (k_phys_body b)) \
    X(int64_t, k_phys_body_shapes, (k_phys_body b, k_phys_shape_slice out)) \
    X(int64_t, k_phys_body_joint_count, (k_phys_body b)) \
    X(int64_t, k_phys_body_joints, (k_phys_body b, k_phys_joint_slice out)) \
    X(int64_t, k_phys_body_contacts, (k_phys_body b, k_phys_manifold_slice out)) \
    X(k_phys_aabb, k_phys_body_aabb, (k_phys_body b)) \
    X(float, k_phys_body_min_extent, (k_phys_body b)) \
    X(k_phys_float3, k_phys_body_max_extent, (k_phys_body b)) \
    X(k_phys_float3, k_phys_body_max_extent_origin, (k_phys_body b)) \
    X(float, k_phys_body_closest_point, (k_phys_body b, k_phys_float3 target, k_phys_float3 *result)) \
    X(k_phys_ray_hit, k_phys_body_cast_ray, (k_phys_body b, k_phys_float3 origin, k_phys_float3 translation, k_phys_query_filter filter, float max_fraction, k_phys_transform body_transform)) \
    X(k_phys_ray_hit, k_phys_body_cast_shape, (k_phys_body b, k_phys_float3 origin, k_phys_float3_slice points, float radius, k_phys_float3 translation, k_phys_query_filter filter, float max_fraction, uint8_t can_encroach, k_phys_transform body_transform)) \
    X(uint8_t, k_phys_body_overlap_shape, (k_phys_body b, k_phys_float3 origin, k_phys_float3_slice points, float radius, k_phys_query_filter filter, k_phys_transform body_transform)) \
    X(int64_t, k_phys_body_collide_mover, (k_phys_body b, k_phys_float3 origin, k_phys_capsule mover, k_phys_query_filter filter, k_phys_transform body_transform, k_phys_plane_hit_slice out)) \
    X(k_phys_toi_hit, k_phys_body_time_of_impact_mover, (k_phys_body b, k_phys_float3 origin, k_phys_capsule mover, k_phys_float3 translation, k_phys_query_filter filter, k_phys_transform transform1, k_phys_transform transform2)) \
    /* Shapes. */ \
    X(k_phys_shape, k_phys_create_sphere_shape, (k_phys_body b, k_phys_shape_def def, k_phys_sphere sphere)) \
    X(k_phys_shape, k_phys_create_capsule_shape, (k_phys_body b, k_phys_shape_def def, k_phys_capsule capsule)) \
    X(k_phys_shape, k_phys_create_box_shape, (k_phys_body b, k_phys_shape_def def, k_phys_float3 half_extents, k_phys_transform frame)) \
    X(k_phys_shape, k_phys_create_hull_shape, (k_phys_body b, k_phys_shape_def def, k_phys_hull hull)) \
    X(k_phys_shape, k_phys_create_transformed_hull_shape, (k_phys_body b, k_phys_shape_def def, k_phys_hull hull, k_phys_transform transform, k_phys_float3 scale)) \
    X(k_phys_shape, k_phys_create_mesh_shape, (k_phys_body b, k_phys_shape_def def, k_phys_mesh mesh, k_phys_float3 scale, k_phys_surface_material_slice materials)) \
    X(k_phys_shape, k_phys_create_height_field_shape, (k_phys_body b, k_phys_shape_def def, k_phys_height_field height_field, k_phys_surface_material_slice materials)) \
    X(k_phys_shape, k_phys_create_compound_shape, (k_phys_body b, k_phys_shape_def def, k_phys_compound compound)) \
    X(void, k_phys_shape_destroy, (k_phys_shape s, uint8_t update_body_mass)) \
    X(uint8_t, k_phys_shape_is_valid, (k_phys_shape s)) \
    X(int32_t, k_phys_shape_type, (k_phys_shape s)) \
    X(k_phys_body, k_phys_shape_body, (k_phys_shape s)) \
    X(k_phys_world, k_phys_shape_world, (k_phys_shape s)) \
    X(uint8_t, k_phys_shape_is_sensor, (k_phys_shape s)) \
    X(void, k_phys_shape_set_name, (k_phys_shape s, k_phys_bytes name)) \
    X(int64_t, k_phys_shape_name, (k_phys_shape s, k_phys_bytes out)) \
    X(void, k_phys_shape_set_user_data, (k_phys_shape s, uint64_t data)) \
    X(uint64_t, k_phys_shape_user_data, (k_phys_shape s)) \
    X(void, k_phys_shape_set_density, (k_phys_shape s, float density, uint8_t update_body_mass)) \
    X(float, k_phys_shape_density, (k_phys_shape s)) \
    X(void, k_phys_shape_set_friction, (k_phys_shape s, float friction)) \
    X(float, k_phys_shape_friction, (k_phys_shape s)) \
    X(void, k_phys_shape_set_restitution, (k_phys_shape s, float restitution)) \
    X(float, k_phys_shape_restitution, (k_phys_shape s)) \
    X(void, k_phys_shape_set_surface_material, (k_phys_shape s, k_phys_surface_material material)) \
    X(k_phys_surface_material, k_phys_shape_surface_material, (k_phys_shape s)) \
    X(int64_t, k_phys_shape_mesh_material_count, (k_phys_shape s)) \
    X(void, k_phys_shape_set_mesh_material, (k_phys_shape s, k_phys_surface_material material, int64_t index)) \
    X(k_phys_surface_material, k_phys_shape_mesh_material, (k_phys_shape s, int64_t index)) \
    X(k_phys_filter, k_phys_shape_filter, (k_phys_shape s)) \
    X(void, k_phys_shape_set_filter, (k_phys_shape s, k_phys_filter filter, uint8_t invoke_contacts)) \
    X(void, k_phys_shape_enable_sensor_events, (k_phys_shape s, uint8_t flag)) \
    X(uint8_t, k_phys_shape_sensor_events_enabled, (k_phys_shape s)) \
    X(void, k_phys_shape_enable_contact_events, (k_phys_shape s, uint8_t flag)) \
    X(uint8_t, k_phys_shape_contact_events_enabled, (k_phys_shape s)) \
    X(void, k_phys_shape_enable_hit_events, (k_phys_shape s, uint8_t flag)) \
    X(uint8_t, k_phys_shape_hit_events_enabled, (k_phys_shape s)) \
    X(k_phys_cast_output, k_phys_shape_ray_cast, (k_phys_shape s, k_phys_float3 origin, k_phys_float3 translation)) \
    X(k_phys_sphere, k_phys_shape_sphere, (k_phys_shape s)) \
    X(k_phys_capsule, k_phys_shape_capsule, (k_phys_shape s)) \
    X(k_phys_hull, k_phys_shape_hull, (k_phys_shape s)) \
    X(k_phys_mesh, k_phys_shape_mesh, (k_phys_shape s)) \
    X(k_phys_float3, k_phys_shape_mesh_scale, (k_phys_shape s)) \
    X(k_phys_height_field, k_phys_shape_height_field, (k_phys_shape s)) \
    X(k_phys_compound, k_phys_shape_compound, (k_phys_shape s)) \
    X(void, k_phys_shape_set_sphere, (k_phys_shape s, k_phys_sphere sphere)) \
    X(void, k_phys_shape_set_capsule, (k_phys_shape s, k_phys_capsule capsule)) \
    X(void, k_phys_shape_set_hull, (k_phys_shape s, k_phys_hull hull)) \
    X(void, k_phys_shape_set_mesh, (k_phys_shape s, k_phys_mesh mesh, k_phys_float3 scale)) \
    X(int64_t, k_phys_shape_contacts, (k_phys_shape s, k_phys_manifold_slice out)) \
    X(int64_t, k_phys_shape_sensor_overlaps, (k_phys_shape s, k_phys_shape_slice out)) \
    X(k_phys_aabb, k_phys_shape_aabb, (k_phys_shape s)) \
    X(k_phys_mass_data, k_phys_shape_mass_data, (k_phys_shape s)) \
    X(k_phys_float3, k_phys_shape_closest_point, (k_phys_shape s, k_phys_float3 target)) \
    X(void, k_phys_shape_apply_wind, (k_phys_shape s, k_phys_float3 wind, float drag, float lift, float max_speed, uint8_t wake)) \
    /* Joints of every kind. */ \
    X(k_phys_distance_joint, k_phys_create_distance_joint, (k_phys_world w, k_phys_distance_joint_def def)) \
    X(k_phys_filter_joint, k_phys_create_filter_joint, (k_phys_world w, k_phys_filter_joint_def def)) \
    X(k_phys_motor_joint, k_phys_create_motor_joint, (k_phys_world w, k_phys_motor_joint_def def)) \
    X(k_phys_parallel_joint, k_phys_create_parallel_joint, (k_phys_world w, k_phys_parallel_joint_def def)) \
    X(k_phys_prismatic_joint, k_phys_create_prismatic_joint, (k_phys_world w, k_phys_prismatic_joint_def def)) \
    X(k_phys_revolute_joint, k_phys_create_revolute_joint, (k_phys_world w, k_phys_revolute_joint_def def)) \
    X(k_phys_spherical_joint, k_phys_create_spherical_joint, (k_phys_world w, k_phys_spherical_joint_def def)) \
    X(k_phys_weld_joint, k_phys_create_weld_joint, (k_phys_world w, k_phys_weld_joint_def def)) \
    X(k_phys_wheel_joint, k_phys_create_wheel_joint, (k_phys_world w, k_phys_wheel_joint_def def)) \
    X(void, k_phys_joint_destroy, (k_phys_joint j, uint8_t wake_bodies)) \
    X(uint8_t, k_phys_joint_is_valid, (k_phys_joint j)) \
    X(int32_t, k_phys_joint_type, (k_phys_joint j)) \
    X(k_phys_body, k_phys_joint_body_a, (k_phys_joint j)) \
    X(k_phys_body, k_phys_joint_body_b, (k_phys_joint j)) \
    X(k_phys_world, k_phys_joint_world, (k_phys_joint j)) \
    X(void, k_phys_joint_set_local_frame_a, (k_phys_joint j, k_phys_transform frame)) \
    X(k_phys_transform, k_phys_joint_local_frame_a, (k_phys_joint j)) \
    X(void, k_phys_joint_set_local_frame_b, (k_phys_joint j, k_phys_transform frame)) \
    X(k_phys_transform, k_phys_joint_local_frame_b, (k_phys_joint j)) \
    X(void, k_phys_joint_set_collide_connected, (k_phys_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_joint_collide_connected, (k_phys_joint j)) \
    X(void, k_phys_joint_set_user_data, (k_phys_joint j, uint64_t data)) \
    X(uint64_t, k_phys_joint_user_data, (k_phys_joint j)) \
    X(void, k_phys_joint_wake_bodies, (k_phys_joint j)) \
    X(uint8_t, k_phys_joint_is_awake, (k_phys_joint j)) \
    X(k_phys_float3, k_phys_joint_constraint_force, (k_phys_joint j)) \
    X(k_phys_float3, k_phys_joint_constraint_torque, (k_phys_joint j)) \
    X(float, k_phys_joint_linear_separation, (k_phys_joint j)) \
    X(float, k_phys_joint_angular_separation, (k_phys_joint j)) \
    X(void, k_phys_joint_set_constraint_tuning, (k_phys_joint j, float hertz, float damping_ratio)) \
    X(k_phys_float2, k_phys_joint_constraint_tuning, (k_phys_joint j)) \
    X(void, k_phys_joint_set_force_threshold, (k_phys_joint j, float threshold)) \
    X(float, k_phys_joint_force_threshold, (k_phys_joint j)) \
    X(void, k_phys_joint_set_torque_threshold, (k_phys_joint j, float threshold)) \
    X(float, k_phys_joint_torque_threshold, (k_phys_joint j)) \
    /* Distance joints. */ \
    X(void, k_phys_distance_set_rest_length, (k_phys_distance_joint j, float length)) \
    X(float, k_phys_distance_rest_length, (k_phys_distance_joint j)) \
    X(void, k_phys_distance_enable_spring, (k_phys_distance_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_distance_spring_enabled, (k_phys_distance_joint j)) \
    X(void, k_phys_distance_set_spring_force_range, (k_phys_distance_joint j, float lower, float upper)) \
    X(k_phys_float2, k_phys_distance_spring_force_range, (k_phys_distance_joint j)) \
    X(void, k_phys_distance_set_spring_hertz, (k_phys_distance_joint j, float hertz)) \
    X(float, k_phys_distance_spring_hertz, (k_phys_distance_joint j)) \
    X(void, k_phys_distance_set_spring_damping_ratio, (k_phys_distance_joint j, float ratio)) \
    X(float, k_phys_distance_spring_damping_ratio, (k_phys_distance_joint j)) \
    X(void, k_phys_distance_enable_limit, (k_phys_distance_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_distance_limit_enabled, (k_phys_distance_joint j)) \
    X(void, k_phys_distance_set_length_range, (k_phys_distance_joint j, float min_length, float max_length)) \
    X(float, k_phys_distance_min_length, (k_phys_distance_joint j)) \
    X(float, k_phys_distance_max_length, (k_phys_distance_joint j)) \
    X(float, k_phys_distance_current_length, (k_phys_distance_joint j)) \
    X(void, k_phys_distance_enable_motor, (k_phys_distance_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_distance_motor_enabled, (k_phys_distance_joint j)) \
    X(void, k_phys_distance_set_motor_speed, (k_phys_distance_joint j, float speed)) \
    X(float, k_phys_distance_motor_speed, (k_phys_distance_joint j)) \
    X(void, k_phys_distance_set_max_motor_force, (k_phys_distance_joint j, float force)) \
    X(float, k_phys_distance_max_motor_force, (k_phys_distance_joint j)) \
    X(float, k_phys_distance_motor_force, (k_phys_distance_joint j)) \
    /* Motor joints. */ \
    X(void, k_phys_motor_set_linear_velocity, (k_phys_motor_joint j, k_phys_float3 velocity)) \
    X(k_phys_float3, k_phys_motor_linear_velocity, (k_phys_motor_joint j)) \
    X(void, k_phys_motor_set_angular_velocity, (k_phys_motor_joint j, k_phys_float3 velocity)) \
    X(k_phys_float3, k_phys_motor_angular_velocity, (k_phys_motor_joint j)) \
    X(void, k_phys_motor_set_max_velocity_force, (k_phys_motor_joint j, float force)) \
    X(float, k_phys_motor_max_velocity_force, (k_phys_motor_joint j)) \
    X(void, k_phys_motor_set_max_velocity_torque, (k_phys_motor_joint j, float torque)) \
    X(float, k_phys_motor_max_velocity_torque, (k_phys_motor_joint j)) \
    X(void, k_phys_motor_set_linear_hertz, (k_phys_motor_joint j, float hertz)) \
    X(float, k_phys_motor_linear_hertz, (k_phys_motor_joint j)) \
    X(void, k_phys_motor_set_linear_damping_ratio, (k_phys_motor_joint j, float ratio)) \
    X(float, k_phys_motor_linear_damping_ratio, (k_phys_motor_joint j)) \
    X(void, k_phys_motor_set_angular_hertz, (k_phys_motor_joint j, float hertz)) \
    X(float, k_phys_motor_angular_hertz, (k_phys_motor_joint j)) \
    X(void, k_phys_motor_set_angular_damping_ratio, (k_phys_motor_joint j, float ratio)) \
    X(float, k_phys_motor_angular_damping_ratio, (k_phys_motor_joint j)) \
    X(void, k_phys_motor_set_max_spring_force, (k_phys_motor_joint j, float force)) \
    X(float, k_phys_motor_max_spring_force, (k_phys_motor_joint j)) \
    X(void, k_phys_motor_set_max_spring_torque, (k_phys_motor_joint j, float torque)) \
    X(float, k_phys_motor_max_spring_torque, (k_phys_motor_joint j)) \
    /* Parallel joints. */ \
    X(void, k_phys_parallel_set_spring_hertz, (k_phys_parallel_joint j, float hertz)) \
    X(float, k_phys_parallel_spring_hertz, (k_phys_parallel_joint j)) \
    X(void, k_phys_parallel_set_spring_damping_ratio, (k_phys_parallel_joint j, float ratio)) \
    X(float, k_phys_parallel_spring_damping_ratio, (k_phys_parallel_joint j)) \
    X(void, k_phys_parallel_set_max_torque, (k_phys_parallel_joint j, float torque)) \
    X(float, k_phys_parallel_max_torque, (k_phys_parallel_joint j)) \
    /* Prismatic joints. */ \
    X(void, k_phys_prismatic_enable_spring, (k_phys_prismatic_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_prismatic_spring_enabled, (k_phys_prismatic_joint j)) \
    X(void, k_phys_prismatic_set_spring_hertz, (k_phys_prismatic_joint j, float hertz)) \
    X(float, k_phys_prismatic_spring_hertz, (k_phys_prismatic_joint j)) \
    X(void, k_phys_prismatic_set_spring_damping_ratio, (k_phys_prismatic_joint j, float ratio)) \
    X(float, k_phys_prismatic_spring_damping_ratio, (k_phys_prismatic_joint j)) \
    X(void, k_phys_prismatic_set_target_translation, (k_phys_prismatic_joint j, float translation)) \
    X(float, k_phys_prismatic_target_translation, (k_phys_prismatic_joint j)) \
    X(void, k_phys_prismatic_enable_limit, (k_phys_prismatic_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_prismatic_limit_enabled, (k_phys_prismatic_joint j)) \
    X(float, k_phys_prismatic_lower_limit, (k_phys_prismatic_joint j)) \
    X(float, k_phys_prismatic_upper_limit, (k_phys_prismatic_joint j)) \
    X(void, k_phys_prismatic_set_limits, (k_phys_prismatic_joint j, float lower, float upper)) \
    X(void, k_phys_prismatic_enable_motor, (k_phys_prismatic_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_prismatic_motor_enabled, (k_phys_prismatic_joint j)) \
    X(void, k_phys_prismatic_set_motor_speed, (k_phys_prismatic_joint j, float speed)) \
    X(float, k_phys_prismatic_motor_speed, (k_phys_prismatic_joint j)) \
    X(void, k_phys_prismatic_set_max_motor_force, (k_phys_prismatic_joint j, float force)) \
    X(float, k_phys_prismatic_max_motor_force, (k_phys_prismatic_joint j)) \
    X(float, k_phys_prismatic_motor_force, (k_phys_prismatic_joint j)) \
    X(float, k_phys_prismatic_translation, (k_phys_prismatic_joint j)) \
    X(float, k_phys_prismatic_speed, (k_phys_prismatic_joint j)) \
    /* Revolute joints. */ \
    X(void, k_phys_revolute_enable_spring, (k_phys_revolute_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_revolute_spring_enabled, (k_phys_revolute_joint j)) \
    X(void, k_phys_revolute_set_spring_hertz, (k_phys_revolute_joint j, float hertz)) \
    X(float, k_phys_revolute_spring_hertz, (k_phys_revolute_joint j)) \
    X(void, k_phys_revolute_set_spring_damping_ratio, (k_phys_revolute_joint j, float ratio)) \
    X(float, k_phys_revolute_spring_damping_ratio, (k_phys_revolute_joint j)) \
    X(void, k_phys_revolute_set_target_angle, (k_phys_revolute_joint j, float radians)) \
    X(float, k_phys_revolute_target_angle, (k_phys_revolute_joint j)) \
    X(float, k_phys_revolute_angle, (k_phys_revolute_joint j)) \
    X(void, k_phys_revolute_enable_limit, (k_phys_revolute_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_revolute_limit_enabled, (k_phys_revolute_joint j)) \
    X(float, k_phys_revolute_lower_limit, (k_phys_revolute_joint j)) \
    X(float, k_phys_revolute_upper_limit, (k_phys_revolute_joint j)) \
    X(void, k_phys_revolute_set_limits, (k_phys_revolute_joint j, float lower, float upper)) \
    X(void, k_phys_revolute_enable_motor, (k_phys_revolute_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_revolute_motor_enabled, (k_phys_revolute_joint j)) \
    X(void, k_phys_revolute_set_motor_speed, (k_phys_revolute_joint j, float speed)) \
    X(float, k_phys_revolute_motor_speed, (k_phys_revolute_joint j)) \
    X(float, k_phys_revolute_motor_torque, (k_phys_revolute_joint j)) \
    X(void, k_phys_revolute_set_max_motor_torque, (k_phys_revolute_joint j, float torque)) \
    X(float, k_phys_revolute_max_motor_torque, (k_phys_revolute_joint j)) \
    /* Spherical joints. */ \
    X(void, k_phys_spherical_enable_cone_limit, (k_phys_spherical_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_spherical_cone_limit_enabled, (k_phys_spherical_joint j)) \
    X(float, k_phys_spherical_cone_limit, (k_phys_spherical_joint j)) \
    X(void, k_phys_spherical_set_cone_limit, (k_phys_spherical_joint j, float radians)) \
    X(float, k_phys_spherical_cone_angle, (k_phys_spherical_joint j)) \
    X(void, k_phys_spherical_enable_twist_limit, (k_phys_spherical_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_spherical_twist_limit_enabled, (k_phys_spherical_joint j)) \
    X(float, k_phys_spherical_lower_twist_limit, (k_phys_spherical_joint j)) \
    X(float, k_phys_spherical_upper_twist_limit, (k_phys_spherical_joint j)) \
    X(void, k_phys_spherical_set_twist_limits, (k_phys_spherical_joint j, float lower, float upper)) \
    X(float, k_phys_spherical_twist_angle, (k_phys_spherical_joint j)) \
    X(void, k_phys_spherical_enable_spring, (k_phys_spherical_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_spherical_spring_enabled, (k_phys_spherical_joint j)) \
    X(void, k_phys_spherical_set_spring_hertz, (k_phys_spherical_joint j, float hertz)) \
    X(float, k_phys_spherical_spring_hertz, (k_phys_spherical_joint j)) \
    X(void, k_phys_spherical_set_spring_damping_ratio, (k_phys_spherical_joint j, float ratio)) \
    X(float, k_phys_spherical_spring_damping_ratio, (k_phys_spherical_joint j)) \
    X(void, k_phys_spherical_set_target_rotation, (k_phys_spherical_joint j, k_phys_quat rotation)) \
    X(k_phys_quat, k_phys_spherical_target_rotation, (k_phys_spherical_joint j)) \
    X(void, k_phys_spherical_enable_motor, (k_phys_spherical_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_spherical_motor_enabled, (k_phys_spherical_joint j)) \
    X(void, k_phys_spherical_set_motor_velocity, (k_phys_spherical_joint j, k_phys_float3 velocity)) \
    X(k_phys_float3, k_phys_spherical_motor_velocity, (k_phys_spherical_joint j)) \
    X(k_phys_float3, k_phys_spherical_motor_torque, (k_phys_spherical_joint j)) \
    X(void, k_phys_spherical_set_max_motor_torque, (k_phys_spherical_joint j, float torque)) \
    X(float, k_phys_spherical_max_motor_torque, (k_phys_spherical_joint j)) \
    /* Weld joints. */ \
    X(void, k_phys_weld_set_linear_hertz, (k_phys_weld_joint j, float hertz)) \
    X(float, k_phys_weld_linear_hertz, (k_phys_weld_joint j)) \
    X(void, k_phys_weld_set_linear_damping_ratio, (k_phys_weld_joint j, float ratio)) \
    X(float, k_phys_weld_linear_damping_ratio, (k_phys_weld_joint j)) \
    X(void, k_phys_weld_set_angular_hertz, (k_phys_weld_joint j, float hertz)) \
    X(float, k_phys_weld_angular_hertz, (k_phys_weld_joint j)) \
    X(void, k_phys_weld_set_angular_damping_ratio, (k_phys_weld_joint j, float ratio)) \
    X(float, k_phys_weld_angular_damping_ratio, (k_phys_weld_joint j)) \
    /* Wheel joints. */ \
    X(void, k_phys_wheel_enable_suspension, (k_phys_wheel_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_wheel_suspension_enabled, (k_phys_wheel_joint j)) \
    X(void, k_phys_wheel_set_suspension_hertz, (k_phys_wheel_joint j, float hertz)) \
    X(float, k_phys_wheel_suspension_hertz, (k_phys_wheel_joint j)) \
    X(void, k_phys_wheel_set_suspension_damping_ratio, (k_phys_wheel_joint j, float ratio)) \
    X(float, k_phys_wheel_suspension_damping_ratio, (k_phys_wheel_joint j)) \
    X(void, k_phys_wheel_enable_suspension_limit, (k_phys_wheel_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_wheel_suspension_limit_enabled, (k_phys_wheel_joint j)) \
    X(float, k_phys_wheel_lower_suspension_limit, (k_phys_wheel_joint j)) \
    X(float, k_phys_wheel_upper_suspension_limit, (k_phys_wheel_joint j)) \
    X(void, k_phys_wheel_set_suspension_limits, (k_phys_wheel_joint j, float lower, float upper)) \
    X(void, k_phys_wheel_enable_spin_motor, (k_phys_wheel_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_wheel_spin_motor_enabled, (k_phys_wheel_joint j)) \
    X(void, k_phys_wheel_set_spin_motor_speed, (k_phys_wheel_joint j, float speed)) \
    X(float, k_phys_wheel_spin_motor_speed, (k_phys_wheel_joint j)) \
    X(void, k_phys_wheel_set_max_spin_torque, (k_phys_wheel_joint j, float torque)) \
    X(float, k_phys_wheel_max_spin_torque, (k_phys_wheel_joint j)) \
    X(float, k_phys_wheel_spin_speed, (k_phys_wheel_joint j)) \
    X(float, k_phys_wheel_spin_torque, (k_phys_wheel_joint j)) \
    X(void, k_phys_wheel_enable_steering, (k_phys_wheel_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_wheel_steering_enabled, (k_phys_wheel_joint j)) \
    X(void, k_phys_wheel_set_steering_hertz, (k_phys_wheel_joint j, float hertz)) \
    X(float, k_phys_wheel_steering_hertz, (k_phys_wheel_joint j)) \
    X(void, k_phys_wheel_set_steering_damping_ratio, (k_phys_wheel_joint j, float ratio)) \
    X(float, k_phys_wheel_steering_damping_ratio, (k_phys_wheel_joint j)) \
    X(void, k_phys_wheel_set_max_steering_torque, (k_phys_wheel_joint j, float torque)) \
    X(float, k_phys_wheel_max_steering_torque, (k_phys_wheel_joint j)) \
    X(void, k_phys_wheel_enable_steering_limit, (k_phys_wheel_joint j, uint8_t flag)) \
    X(uint8_t, k_phys_wheel_steering_limit_enabled, (k_phys_wheel_joint j)) \
    X(float, k_phys_wheel_lower_steering_limit, (k_phys_wheel_joint j)) \
    X(float, k_phys_wheel_upper_steering_limit, (k_phys_wheel_joint j)) \
    X(void, k_phys_wheel_set_steering_limits, (k_phys_wheel_joint j, float lower, float upper)) \
    X(void, k_phys_wheel_set_target_steering_angle, (k_phys_wheel_joint j, float radians)) \
    X(float, k_phys_wheel_target_steering_angle, (k_phys_wheel_joint j)) \
    X(float, k_phys_wheel_steering_angle, (k_phys_wheel_joint j)) \
    X(float, k_phys_wheel_steering_torque, (k_phys_wheel_joint j)) \
    /* Contacts. */ \
    X(uint8_t, k_phys_contact_is_valid, (k_phys_contact c)) \
    X(int64_t, k_phys_contact_manifolds, (k_phys_contact c, k_phys_manifold_slice out)) \
    /* Hulls. */ \
    X(k_phys_hull, k_phys_create_hull, (k_phys_float3_slice points, int64_t max_vertices)) \
    X(k_phys_hull, k_phys_create_box_hull, (k_phys_float3 half_extents, k_phys_transform frame)) \
    X(k_phys_hull, k_phys_create_cylinder_hull, (float height, float radius, float y_offset, int64_t sides)) \
    X(k_phys_hull, k_phys_create_cone_hull, (float height, float radius1, float radius2, int64_t slices)) \
    X(k_phys_hull, k_phys_create_rock_hull, (float radius)) \
    X(k_phys_hull, k_phys_hull_transformed, (k_phys_hull h, k_phys_transform transform, k_phys_float3 scale)) \
    X(void, k_phys_hull_destroy, (k_phys_hull h)) \
    X(uint8_t, k_phys_hull_is_valid, (k_phys_hull h)) \
    X(k_phys_hull_info, k_phys_hull_info_of, (k_phys_hull h)) \
    X(int64_t, k_phys_hull_vertices, (k_phys_hull h, k_phys_float3_slice out)) \
    X(int64_t, k_phys_hull_triangles, (k_phys_hull h, k_phys_int3_slice out)) \
    /* Meshes. */ \
    X(k_phys_mesh, k_phys_create_mesh, (k_phys_float3_slice vertices, k_phys_i32_slice indices, k_phys_bytes material_indices, k_phys_mesh_def def)) \
    X(k_phys_mesh, k_phys_create_grid_mesh, (int64_t x_count, int64_t z_count, float cell_width, int64_t material_count, uint8_t identify_edges)) \
    X(k_phys_mesh, k_phys_create_wave_mesh, (int64_t x_count, int64_t z_count, float cell_width, float amplitude, float row_frequency, float column_frequency)) \
    X(k_phys_mesh, k_phys_create_torus_mesh, (int64_t radial_resolution, int64_t tubular_resolution, float radius, float thickness)) \
    X(k_phys_mesh, k_phys_create_box_mesh, (k_phys_float3 center, k_phys_float3 extent, uint8_t identify_edges)) \
    X(k_phys_mesh, k_phys_create_hollow_box_mesh, (k_phys_float3 center, k_phys_float3 extent)) \
    X(k_phys_mesh, k_phys_create_platform_mesh, (k_phys_float3 center, float height, float top_width, float bottom_width)) \
    X(void, k_phys_mesh_destroy, (k_phys_mesh m)) \
    X(uint8_t, k_phys_mesh_is_valid, (k_phys_mesh m)) \
    X(k_phys_mesh_info, k_phys_mesh_info_of, (k_phys_mesh m)) \
    X(int64_t, k_phys_mesh_vertices, (k_phys_mesh m, k_phys_float3_slice out)) \
    X(int64_t, k_phys_mesh_triangles, (k_phys_mesh m, k_phys_int3_slice out)) \
    /* Height fields. */ \
    X(k_phys_height_field, k_phys_create_height_field, (k_phys_f32_slice heights, k_phys_bytes material_indices, k_phys_height_field_def def)) \
    X(k_phys_height_field, k_phys_create_grid_height_field, (int64_t rows, int64_t columns, k_phys_float3 scale, uint8_t make_holes)) \
    X(k_phys_height_field, k_phys_create_wave_height_field, (int64_t rows, int64_t columns, k_phys_float3 scale, float row_frequency, float column_frequency, uint8_t make_holes)) \
    X(void, k_phys_height_field_destroy, (k_phys_height_field h)) \
    X(uint8_t, k_phys_height_field_is_valid, (k_phys_height_field h)) \
    X(k_phys_height_field_info, k_phys_height_field_info_of, (k_phys_height_field h)) \
    /* Baked compounds, for static bodies. */ \
    X(k_phys_compound, k_phys_create_compound, (k_phys_compound_sphere_slice spheres, k_phys_compound_capsule_slice capsules, k_phys_compound_hull_slice hulls, k_phys_compound_mesh_slice meshes)) \
    X(void, k_phys_compound_destroy, (k_phys_compound c)) \
    X(uint8_t, k_phys_compound_is_valid, (k_phys_compound c)) \
    X(k_phys_compound_info, k_phys_compound_info_of, (k_phys_compound c)) \
    /* Geometry on its own, outside any world. */ \
    X(k_phys_mass_data, k_phys_sphere_mass, (k_phys_sphere s, float density)) \
    X(k_phys_mass_data, k_phys_capsule_mass, (k_phys_capsule c, float density)) \
    X(k_phys_mass_data, k_phys_hull_mass, (k_phys_hull h, float density)) \
    X(k_phys_aabb, k_phys_sphere_aabb, (k_phys_sphere s, k_phys_transform transform)) \
    X(k_phys_aabb, k_phys_capsule_aabb, (k_phys_capsule c, k_phys_transform transform)) \
    X(k_phys_aabb, k_phys_hull_aabb, (k_phys_hull h, k_phys_transform transform)) \
    X(k_phys_aabb, k_phys_mesh_aabb, (k_phys_mesh m, k_phys_transform transform, k_phys_float3 scale)) \
    X(k_phys_aabb, k_phys_height_field_aabb, (k_phys_height_field h, k_phys_transform transform)) \
    X(k_phys_aabb, k_phys_compound_aabb, (k_phys_compound c, k_phys_transform transform)) \
    X(k_phys_cast_output, k_phys_sphere_ray_cast, (k_phys_sphere s, k_phys_float3 origin, k_phys_float3 translation, float max_fraction)) \
    X(k_phys_cast_output, k_phys_capsule_ray_cast, (k_phys_capsule c, k_phys_float3 origin, k_phys_float3 translation, float max_fraction)) \
    X(k_phys_cast_output, k_phys_hull_ray_cast, (k_phys_hull h, k_phys_float3 origin, k_phys_float3 translation, float max_fraction)) \
    X(k_phys_cast_output, k_phys_mesh_ray_cast, (k_phys_mesh m, k_phys_float3 scale, k_phys_float3 origin, k_phys_float3 translation, float max_fraction)) \
    X(k_phys_cast_output, k_phys_height_field_ray_cast, (k_phys_height_field h, k_phys_float3 origin, k_phys_float3 translation, float max_fraction)) \
    X(k_phys_cast_output, k_phys_compound_ray_cast, (k_phys_compound c, k_phys_float3 origin, k_phys_float3 translation, float max_fraction)) \
    X(uint8_t, k_phys_sphere_overlap, (k_phys_sphere s, k_phys_transform transform, k_phys_float3_slice points, float radius)) \
    X(uint8_t, k_phys_capsule_overlap, (k_phys_capsule c, k_phys_transform transform, k_phys_float3_slice points, float radius)) \
    X(uint8_t, k_phys_hull_overlap, (k_phys_hull h, k_phys_transform transform, k_phys_float3_slice points, float radius)) \
    X(uint8_t, k_phys_mesh_overlap, (k_phys_mesh m, k_phys_float3 scale, k_phys_transform transform, k_phys_float3_slice points, float radius)) \
    X(uint8_t, k_phys_height_field_overlap, (k_phys_height_field h, k_phys_transform transform, k_phys_float3_slice points, float radius)) \
    X(uint8_t, k_phys_compound_overlap, (k_phys_compound c, k_phys_transform transform, k_phys_float3_slice points, float radius)) \
    X(k_phys_cast_output, k_phys_sphere_shape_cast, (k_phys_sphere s, k_phys_float3_slice points, float radius, k_phys_float3 translation, float max_fraction, uint8_t can_encroach)) \
    X(k_phys_cast_output, k_phys_capsule_shape_cast, (k_phys_capsule c, k_phys_float3_slice points, float radius, k_phys_float3 translation, float max_fraction, uint8_t can_encroach)) \
    X(k_phys_cast_output, k_phys_hull_shape_cast, (k_phys_hull h, k_phys_float3_slice points, float radius, k_phys_float3 translation, float max_fraction, uint8_t can_encroach)) \
    X(k_phys_cast_output, k_phys_mesh_shape_cast, (k_phys_mesh m, k_phys_float3 scale, k_phys_float3_slice points, float radius, k_phys_float3 translation, float max_fraction, uint8_t can_encroach)) \
    X(k_phys_cast_output, k_phys_height_field_shape_cast, (k_phys_height_field h, k_phys_float3_slice points, float radius, k_phys_float3 translation, float max_fraction, uint8_t can_encroach)) \
    X(k_phys_cast_output, k_phys_compound_shape_cast, (k_phys_compound c, k_phys_float3_slice points, float radius, k_phys_float3 translation, float max_fraction, uint8_t can_encroach)) \
    X(k_phys_distance_output, k_phys_shape_distance, (k_phys_float3_slice points_a, float radius_a, k_phys_float3_slice points_b, float radius_b, k_phys_transform b_in_a, uint8_t use_radii)) \
    X(k_phys_cast_output, k_phys_shape_cast_pair, (k_phys_float3_slice points_a, float radius_a, k_phys_float3_slice points_b, float radius_b, k_phys_transform b_in_a, k_phys_float3 translation_b, float max_fraction, uint8_t can_encroach)) \
    X(k_phys_toi_output, k_phys_time_of_impact, (k_phys_float3_slice points_a, float radius_a, k_phys_sweep sweep_a, k_phys_float3_slice points_b, float radius_b, k_phys_sweep sweep_b, float max_fraction)) \
    X(k_phys_transform, k_phys_sweep_transform, (k_phys_sweep sweep, float time)) \
    X(int64_t, k_phys_solve_planes, (k_phys_float3 target_delta, k_phys_collision_plane_slice planes, k_phys_float3 *delta)) \
    X(k_phys_float3, k_phys_clip_vector, (k_phys_float3 vector, k_phys_collision_plane_slice planes))

#define K_PHYS_PROTO(ret, name, params) ret name params;
K_PHYS_API(K_PHYS_PROTO)
#undef K_PHYS_PROTO

/* The constants physics.k declares, with their K types; here
   they are K_PHYS_<name>. Where one is Box3D's own enum, it has Box3D's
   value. */
#define K_PHYS_CONSTANTS(X) \
    /* Body types, b3BodyType. */ \
    X(i32, STATIC, 0) \
    X(i32, KINEMATIC, 1) \
    X(i32, DYNAMIC, 2) \
    /* Shape types, b3ShapeType. */ \
    X(i32, SHAPE_CAPSULE, 0) \
    X(i32, SHAPE_COMPOUND, 1) \
    X(i32, SHAPE_HEIGHT_FIELD, 2) \
    X(i32, SHAPE_HULL, 3) \
    X(i32, SHAPE_MESH, 4) \
    X(i32, SHAPE_SPHERE, 5) \
    /* Joint types, b3JointType. */ \
    X(i32, JOINT_PARALLEL, 0) \
    X(i32, JOINT_DISTANCE, 1) \
    X(i32, JOINT_FILTER, 2) \
    X(i32, JOINT_MOTOR, 3) \
    X(i32, JOINT_PRISMATIC, 4) \
    X(i32, JOINT_REVOLUTE, 5) \
    X(i32, JOINT_SPHERICAL, 6) \
    X(i32, JOINT_WELD, 7) \
    X(i32, JOINT_WHEEL, 8) \
    /* How two shapes' friction or restitution combine; DEFAULT is Box3D's: */ \
    /* the geometric mean for friction, the larger for restitution. */ \
    X(i32, MIX_DEFAULT, 0) \
    X(i32, MIX_GEOMETRIC, 1) \
    X(i32, MIX_MIN, 2) \
    X(i32, MIX_MAX, 3) \
    X(i32, MIX_AVERAGE, 4) \
    X(i32, MIX_MULTIPLY, 5) \
    /* Time of impact states, b3TOIState. */ \
    X(i32, TOI_UNKNOWN, 0) \
    X(i32, TOI_FAILED, 1) \
    X(i32, TOI_OVERLAPPED, 2) \
    X(i32, TOI_HIT, 3) \
    X(i32, TOI_SEPARATED, 4) \
    /* Limits. */ \
    X(i32, MAX_WORKERS, 32) \
    X(i32, MAX_PROXY_POINTS, 128) \
    X(i32, MAX_HULL_VERTICES, 128) \
    X(i32, MAX_MANIFOLD_POINTS, 4) \
    X(i32, HEIGHT_FIELD_HOLE, 255)

#define K_PHYS_ENUM(type, name, value) K_PHYS_##name = value,
enum { K_PHYS_CONSTANTS(K_PHYS_ENUM) };
#undef K_PHYS_ENUM

#ifdef __cplusplus
}
#endif

#endif
