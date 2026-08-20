# The rotation campaign: row formulation, migration, slice sequence

Design study. **No code was changed to produce this document.** Every claim
about the engine cites `file:line` at the state of branch `fix/physics-surgery`
at merge `09c0654`. Where a claim is inferred rather than measured, it says so
in the sentence that makes it.

The campaign's subject: today bodies translate off contacts but do not tip.
Contacts carry no torque. The diagnosed keystone is a constraint row carrying a
**full Jacobian** (linear and angular DOFs coupled in one row) replacing pairs
of independent scalar rows that correct each other.

---

## 0. Method, and what this study established

Read in full: `src/core/physics_system_v4.cpp` (the relevant mechanisms),
`include/logosphere/physics/physics_solver.h`,
`include/logosphere/physics/narrow_phase.h`,
`include/logosphere/physics/contact_manifold.h`, `src/particle_core.h`,
`src/materials.h`, all 30 rows of `tests/invariants/INVARIANTS.jsonl`,
`tests/invariants/LEDGER.md`, `docs/todo_plans/PHYSICS_PIPELINE_SEQUENCE.md`,
the three acceptance tests, and the parked branch `feat/joint-block-solver`
via `git show` (never checked out).

Six findings that were not in the brief and that change the plan:

1. **`include/logosphere/physics/physics_solver.h` exists.** INV-16 and the
   code comment at `src/core/physics_system_v4.cpp:3023` both cite it as if it
   were missing from the tree. It is present, and the PIVOT LAW block is at
   `include/logosphere/physics/physics_solver.h:177-191`.

2. **`pivot_inertia_a` / `pivot_inertia_b` are dead fields.** Declared at
   `include/logosphere/physics/physics_solver.h:188-189`, never written and
   never read anywhere in the tree. The only other occurrence of the token is
   the comment pointing at them (`src/core/physics_system_v4.cpp:3023`). Half
   of INV-16 is present as a struct field and absent as a mechanism.

3. **The full-Jacobian row already exists, on a parked branch.**
   `feat/joint-block-solver` commit `4c92518` implements `A[i][j] = J_i M⁻¹
   J_jᵀ` as a 4x4 direct solve per joint. It measured stalled solver exits
   5571 -> 250 (-96%) and true convergences 1623 -> 7068. It was parked for two
   named reasons, **both of which have since landed on `fix/physics-surgery`**
   (§1.7). This is a revival, not a green field.

4. **`ANGULAR_DRAG = 0.95` is applied per substep, unconditionally, to every
   body.** `src/generated/physics_constants.h:301`, applied at
   `src/core/physics_system_v4.cpp:4790` and `:4814-4815`, inside the substep
   loop (`:353`). Four substeps per frame gives 0.95⁴ = 0.815: an 18.5%
   angular-velocity sink per frame with no dissipation story behind it. This
   will fight any contact-generated tipping, and it is a precondition, not a
   footnote (§5, S3).

5. **A sleeping body keeps rotating.** `update_rest_state` zeroes `vx/vy/vz`
   on entering rest (`src/core/physics_system_v4.cpp:4643`) but not `omega_*`,
   and its rest test reads linear velocity only (`:4623`).
   `integrate_angular_velocities` skips KINEMATIC bodies only (`:4749`), not
   sleepers. A body spinning in place with zero linear velocity sleeps and then
   keeps spinning, decaying only by ANGULAR_DRAG. This is the angular half of
   INV-7 and INV-18, missing.

6. **Four inertia models coexist, and two of them are wrong in different
   ways** (§1.3). The engine's own comment quantifies the error at 130x for a
   grass blade (`src/particle_core.h:264-271`).

**Not established, stated as such:** the runtime verdict of the rotation
ladder. `TEST_AUDIT.jsonl:120` says "rung 3 red"; the file header of
`tests/test_rotation_ladder.cpp:11-18` says rung 1; commit `342fc2a` claimed
4/4 pass. The last recorded sweep row in `build-release/sweep_results.json`
reads `FAIL, rc=1, 0.9 s`, which is far too short for the roughly 2500
simulated frames the test schedules, so that row almost certainly records an
early exit rather than a rung verdict. This study ran nothing (read-only), so
the ladder's true current state is **unknown** and is slice S0's first job.

---

## 1. What exists today

### 1.1 The anchor-torque half-machinery

What it does:

- Gluon rows store world-space lever arms to the bond anchor:
  `c.anchor_rax/ray/raz` and `c.anchor_rbx/rby/rbz`, set at
  `src/core/physics_system_v4.cpp:1887-1888`, declared at
  `include/logosphere/physics/physics_solver.h:190-191`.
- `c.apply_anchor_torque` is a bool set from the inverse of the
  `ANCHOR_TORQUE_OFF` env lever (`src/core/physics_system_v4.cpp:1885-1886`).
- The row's effective mass includes an angular term:
  `K = 1/m_a + 1/m_b + (r_a x J)²/I_a + (r_b x J)²/I_b`
  (`:1893-1913`). The comment states the reason exactly: "or every impulse
  over-corrects the anchor it now spins".
- The row measures velocity **at the anchor**, adding omega x r to `v_rel`
  (`:3110-3125`). The comment records what its absence cost: "the 15 m fling
  of rung 3's first torque attempt".
- The applied linear impulse also torques the bodies:
  `omega += (r x J·impulse) / I` (`:3240-3275`).

What it does **not** do, and this is the whole gap:

- **No reciprocal coupling.** An angular impulse does not carry
  `dv = -(dω x r)`. The angular solve at `:3033-3045` writes `omega_*` only.
  Consequence, measured and quoted at
  `include/logosphere/physics/physics_solver.h:183-187`: "the drive rotated
  about the centre, which displaced the anchor, which the anchor rows
  immediately undid: measured +3.3 rad/s of drive against -3.3 rad/s of anchor
  correction, net 0.001, forever".
- **No pivot inertia.** `pivot_inertia_a/b` never written (§0.2). The row is
  priced with centre inertia, not anchor-axis inertia.
- **Gated on a game category.** Both the K contribution (`:1895`, `:1905`) and
  the apply (`:3255`, `:3268`) require `is_quat_driven && owner ==
  ParticleOwner::PHYSICS`. These are among the seven INV-15 owner-reads the
  ledger ruled a fix task (#43); `test_inv15_owner_blindness` pins them by line
  number, including `1876/1886/3225/3236`.
- **Two lever arms, two world points.** `anchor_ra` and `anchor_rb` are
  computed from each body's own attachment offset. For a strained bond those
  are *different world points*, and an impulse pair applied at two different
  points does not conserve angular momentum. See §2.3: this is why the door
  must take one application point rather than two arms.
- **Isotropic inertia.** Both the K build (`:1898`, `:1908`) and the apply
  (`:3245`, `:3253`, `:3265`) use `GetMomentOfInertia()`, the scalar. §1.3.
- **Contacts have none of it.** No contact row anywhere sets `anchor_r*`;
  `apply_anchor_torque` defaults false
  (`include/logosphere/physics/physics_solver.h:176`).

### 1.2 Contacts carry no torque

Confirmed. Box-box rows are built at `src/core/physics_system_v4.cpp:1267-1279`
with `jx/jy/jz` from the manifold normal and no lever arm. Turtle rows at
`:783-791` hardcode the Jacobian to `(0,0,1)` with no contact point at all.

**The contact point already exists for box-box.**
`ContactPoint::px/py/pz` is populated by every narrow-phase handler
(`include/logosphere/physics/contact_manifold.h:18-23`) and is read today at
`src/core/physics_system_v4.cpp:1367-1369` **solely to fill a
`CollisionEvent`**. The geometry needed for a lever arm is computed, carried
into the solver, and thrown away. That makes box-box the cheapest first
conversion (§5, S4).

The turtle path is the opposite: it computes only a *Z extent* for a rotated
box (`:764-776`, via `aabb_of_obb(obb_of_box_particle(...))`) and deliberately
never locates the supporting vertex horizontally. Giving the turtle row a lever
arm requires new geometry (the support point of an OBB against a plane).

### 1.3 Inertia: four models, two of them wrong

| Model | Where | What it returns | Used by |
|---|---|---|---|
| Scalar, shape-approximate | `src/particle_core.h:241-259` | one float; BOX case is `0.5·m·r²` with `r = (width+height)/4`, ignoring thickness entirely | free-body integration `:4759`, `:4808-4809`; anchor-torque K `:1898,1908`; anchor-torque apply `:3245,3253,3265`; `angular_damping` derivation `:5019`; the parked block solver's `inv_Ia/inv_Ib` |
| Axis projection | `src/particle_core.h:276-312` | `n̂ᵀ I n̂` from real principal moments, rotated into body frame | angular row build `:1957-1958`; quat-drive row sizing `:2266-2270`; angular row solve `:3033-3034` |
| Per-particle override | `src/particle_core.h:67, 242` | a stored scalar that short-circuits model 1 | anything that sets it |
| Tensor | nowhere | | |

The engine states the size of the model-1 error itself, at
`src/particle_core.h:264-271`:

> "a 40 x 3 x 300 mm grass blade gets 5.8e-5*m where its true transverse
> inertia is 7.5e-3*m, so the solver believes the blade is 130x easier to tip
> over than it is. Nothing catches it because the error is internally
> consistent: the same wrong number is used to size the impulse and to apply
> it."

**Model 2 is also not what a full-Jacobian row needs, and this is subtle.**
`GetInertiaAboutAxis(n̂)` returns `n̂ᵀ I n̂`. A row needs `I⁻¹` as an *operator*:
`Δω = I⁻¹ L`, and `I⁻¹L` is not parallel to `L` unless `L` lies along a
principal axis. Moreover

    n̂ᵀ I⁻¹ n̂  ≠  1 / (n̂ᵀ I n̂)

except when `n̂` is a principal axis or `I` is isotropic. So using
`1/GetInertiaAboutAxis(n̂)` as an inverse inertia is a second, quieter error on
top of the first. Today's angular solve (`:3033-3045`) sizes with `n̂ᵀ I n̂` and
then *forces* `Δω` along `n̂`. That is self-consistent as a "gear" model and it
is not Newton-Euler; the full-Jacobian row replaces that semantic.

**Upgrade path (§5, S1): one tensor function.** `I_world⁻¹ = R diag(1/Ixx,
1/Iyy, 1/Izz) Rᵀ`, derived from the same extents and orientation the two
existing accessors already read. No new stored state, which is the identical
argument `src/particle_core.h:272-275` makes for `GetInertiaAboutAxis`. Both
existing accessors then become consumers of the one function so they cannot
disagree (INV-28's principle, applied to inertia rather than to attachment
points).

### 1.4 Orientation truth (owner ruling DEFERRED)

`tests/invariants/LEDGER.md:155-157`:

> one-orientation-truth (miner candidate 14): ruling DEFERRED by the owner
> until the rotation-ladder work forces the representation question. Parked
> here; not written as an invariant.

The state of the representation, at `src/core/physics_system_v4.cpp:4743-4841`:

- Torque integrates into `omega_z` (scalar path, `:4772-4796`) and separately
  into `omega_x/omega_y` (3-axis path, `:4808-4815`), both dividing by the
  **scalar** `I` from `:4759`.
- `rotation_q` integrates from the full omega vector via the axis-angle
  exponential (`:4823-4833`).
- The Euler triple is published from the quaternion **only if
  `p.is_quat_driven`** (`:4838-4840`). The comment names the consequence:
  "Particles that aren't quat-driven keep their Euler-owned path; rotation_q on
  those particles is a stale by-product".

So the authoritative orientation is per-body and depends on a flag.
`obb_of_box_particle` already has to branch on it
(`include/logosphere/physics/narrow_phase.h:82-86`: "rotation_q when the
particle is quat-driven, otherwise the Euler triple composed X then Y then Z").
The moment `I_world⁻¹` needs one `R` per body, every consumer needs one answer.
**§5 names S6 as the slice where this ruling must land** and §6.1 gives the
options.

Two more angular-integration facts that bear on the campaign:

- `MAX_OMEGA = 6.28 rad/s` (`src/generated/physics_constants.h:250`) is clamped
  **per axis** (`:4788-4789`, `:4810-4813`), not on the vector magnitude. An
  axis-aligned clamp on a vector quantity is frame-dependent: the same physical
  spin clips differently depending on world orientation. That is the same class
  of defect as an axis-aligned friction basis (§1.5).
- `ANGULAR_DRAG = 0.95` per substep (§0.4). Its comment calls it "general air
  resistance for rotation", but it carries no area, no density, no relative
  velocity, and applies in vacuum to every body. Under INV-19
  (`damping-is-physical-dissipation`) that is damping outside a dissipation
  story.

### 1.5 Friction tangents are axis-aligned picks

`src/core/physics_system_v4.cpp:3314-3333`: the two tangents are selected by
which component of the normal exceeds 0.5 and are then set to world axis unit
vectors. They are not orthogonalized against the true normal, so for any
oblique contact `t1` and `t2` are not perpendicular to `n̂` and the "tangent"
rows have a normal-direction component.

Two further defects in the same block:

- Both tangent rows use `c.effective_mass` (`:3345`, `:3371`), which is the
  **normal row's** effective mass. Correct only when the tangential effective
  mass equals the normal one, which is true for a lever-arm-free pair of point
  masses and false in general (and always false once lever arms exist).
- The Coulomb limit is applied as two independent box clamps
  (`:3347-3349`, `:3373-3375`), not as a disc `|f_t| <= mu·f_n`. Two box clamps
  admit a friction force up to `sqrt(2)·mu·f_n` along the diagonal.

### 1.6 Torsion is the owed sibling

`src/materials.h:174-179` lays out the family:

    axial     k = E·A / L
    bending   K = E·I / L      I = second moment of area
    torsion   K = G·J / L      J = polar second moment
    damping   c = eta · sqrt(k·m)

Axial landed (`src/core/physics_system_v4.cpp:5006`). Bending landed
(`:5017-5018`, with `I_sec = A²/(4π)`). **Torsion is not implemented anywhere**
(grep over `src/ include/ tests/ schema/` finds only the comment and unrelated
animation uses of the word). `G` is deliberately not declared:
`src/materials.h:186-187` says "G = E / (2(1+nu)) for an isotropic solid, so
declaring it would be a fourth place for the three to disagree", so the
derivation input already exists.

### 1.7 Prior art: `feat/joint-block-solver`

Local-only branch, five commits unique against `origin/main`, sharing merge
base `e8ca0d9` with `fix/physics-surgery`. Both branches are answers to the
same RCA. **No remnants exist on `fix/physics-surgery`**: `solve_joint_block`,
`JOINT_BLOCK_OFF`, `BLOCK_NO_BIAS` and `GluonConstraintIndices::ang_idx` all
return zero hits on HEAD.

What it built (`git show feat/joint-block-solver:src/core/physics_system_v4.cpp`,
lines at branch tip):

- Scope gate `:2379-2397`: only `force_bounded()` (compliant/organic) joints
  enter a block. Welds and servos stayed sequential, because a simultaneous
  solve returns a compromise between pose and anchor that a rigid joint must
  not accept. Both humanoid drive tests failed when the block was applied to
  everything.
- Generalized Jacobian per row `:2430-2478`: `L[i]` linear direction, `Ga[i] =
  r_a x J`, `Hb[i] = -(r_b x J)`, and for angular rows `Ga = axis`, `Hb =
  -axis`.
- The matrix `:2480-2491`, verbatim: `A[i][j] = ll * (inv_ma + inv_mb) + gg *
  inv_Ia + hh * inv_Ib;` with `A[i][i] += 1e-9` as the ridge "to keep the
  factorisation defined at singularity".
- Gaussian elimination with partial pivoting, `n <= 4`, `:2493-2512`.
- A coherence fix `:2427-2438` that is itself a finding: the sequential paths
  disagreed about immovability (linear treated `is_at_rest` as immovable,
  angular honored only KINEMATIC), which inside one simultaneous solve means "a
  body that cannot translate yet spins freely: energy from an inconsistency".
  Rung 3 was red until that landed. On HEAD this disagreement still exists
  (`:3033-3045` gates on KINEMATIC only; `inv_mass_momentum` at `:483-489`
  includes `is_at_rest`), which is why §5's S2 comes before any row work.

Why it was parked, and why that matters now. Commit `1f5cdd4` RCA'd the
residual wiggle with three A/Bs (block ON: 1.081 m/s and accelerating on a body
nobody was touching; block OFF: 0.0000; block ON with `BLOCK_NO_BIAS=1`:
0.0000) and concluded:

> "A sequential sweep always UNDERSHOOTS its target, and that undershoot was
> quietly acting as damping all along. A direct solve hits the target exactly,
> so the whole correction becomes kinetic energy, every substep, and compounds."

Commit `2862edd` then added split impulse for block rows, fixed the wiggle,
restored rungs 1-2, and regressed rungs 3-4: "Geometric repair moves a pose but
creates no sustained torque... the honest next step is a restoring torque that
is a function of angle rather than a position correction in disguise."

**Both named blockers have since landed on `fix/physics-surgery`, independently
of that branch:**

- Universal split impulse (`60bc585`, and the scoped angular split in
  `a50218a`). The velocity phase carries no position bias for constraint rows,
  which is exactly the discipline `1f5cdd4` said the block rows lacked. See the
  split rules at `src/core/physics_system_v4.cpp:2968-2999`.
- An angle-dependent restoring torque derived from material and geometry:
  bending `K = E·I/L` at `:5017-5018` (`7a14af1`), with the per-row torque
  budget `(k_ang·e_mag + c_ang·|ω_rel|)·dt` at `:2286-2296`. That is precisely
  "a restoring torque that is a function of angle".

**The block solver's own defects to fix on revival:** `inv_Ia`/`inv_Ib` are
isotropic scalars from `GetMomentOfInertia()` (§1.3, the 130x error);
immovability is hand-rolled inline rather than going through
`inv_mass_momentum` (which post-dates the branch, `2c18ee3`); and the anchor
velocity term is gated on `is_quat_driven` alone rather than the HEAD gating.

---

## 2. THE ROW FORMULATION

### 2.1 The row

For a pair `(a, b)`, a unit constraint direction `n̂` in world space, and a
single world application point `P`:

    r_a = P - x_a          r_b = P - x_b

    J = [ n̂ᵀ , (r_a x n̂)ᵀ , -n̂ᵀ , -(r_b x n̂)ᵀ ]          (1x12)
    u = [ v_a , ω_a , v_b , ω_b ]ᵀ                          (12x1)

    C_dot = J u
          = n̂ · [ (v_a + ω_a x r_a) - (v_b + ω_b x r_b) ]

The second line is the identity `(r x n̂)·ω = n̂·(ω x r)`. `C_dot` is the
relative velocity of the two bodies' **material points at P**, along `n̂`. This
is the whole content of the formulation: the row measures the velocity of the
point where the constraint acts, not the velocity of two centres.

Sign convention matches the engine's existing rows
(`include/logosphere/physics/physics_solver.h:60-71`: `v_rel = ĵ·(v_a - v_b)`,
`a` pushed along `+ĵ`, `b` along `-ĵ`), so no consumer flips.

**Effective mass.**

    K = J M⁻¹ Jᵀ
      = 1/m_a + 1/m_b
        + (r_a x n̂)ᵀ I_a⁻¹ (r_a x n̂)
        + (r_b x n̂)ᵀ I_b⁻¹ (r_b x n̂)

    m_eff = 1 / K

**Apply**, for a scalar impulse λ:

    Δv_a = + n̂ λ / m_a            Δω_a = + I_a⁻¹ (r_a x n̂) λ
    Δv_b = - n̂ λ / m_b            Δω_b = - I_b⁻¹ (r_b x n̂) λ

`I⁻¹` is the world-space inverse inertia **tensor**. `Δω` is generally not
parallel to `r x n̂`. §1.3 is why neither existing accessor can supply this.

**Angular momentum is conserved by construction, and only because both bodies
use the same P.** About any fixed origin:

    ΔL = (x_a x n̂λ + I_a Δω_a) + (x_b x (-n̂λ) + I_b Δω_b)
       = λ [ (x_a + r_a) x n̂ - (x_b + r_b) x n̂ ]
       = λ [ P x n̂ - P x n̂ ]
       = 0

Two independent lever arms (today's `anchor_ra`, `anchor_rb`, §1.1) do not give
this. That single line decides the door's signature (§2.3).

**INV-16 is satisfied in effect, not in form.** The invariant's statement names
"the anchor-axis inertia (I_com + m·r_perp²)". The formulation above never
forms that quantity: the parallel-axis term is implicit in
`(r x n̂)ᵀ I⁻¹ (r x n̂)`, and the constrained-rotation-about-P behavior falls out
of the row measuring `C_dot` at `P`. `pivot_inertia_a/b` are therefore deleted,
not populated. This is an owner question (§6.5): the physics is right, the
wording names a representation the correct formulation does not use.

### 2.2 Scalar effective mass or block K, and where each

Both, in different places, and the split is decided by measurement rather than
taste.

**Scalar `1/(J M⁻¹ Jᵀ)` with sequential impulses, for contact normal rows.**
One row per manifold point. Rows sharing a normal are near-redundant, which is
what the `eff_mass_share` split already treats
(`include/logosphere/physics/physics_solver.h:97-103`). No measurement in the
tree shows contact rows need a block, and the sequential path is what every
existing exit criterion, warm start and cap is written against.

**Block `K` (direct solve) for hard-coupled row groups.** Specifically a
joint's three anchor rows plus its drive row. The evidence is measured and
specific, from `feat/joint-block-solver` commit `4c92518`:

> "For our blade the 2x2 core of that matrix is nearly singular (determinant
> under half a percent of its terms: the rows are almost the same constraint
> counted twice), which is exactly the case Gauss-Seidel cannot iterate through
> and a direct solve does not care about."

The structural reason: for a point anchor,

    K₃ₓ₃ = (1/m_a + 1/m_b)·1₃ - [r_a]ˣ I_a⁻¹ [r_a]ˣ - [r_b]ˣ I_b⁻¹ [r_b]ˣ

The angular terms are rank-2 (singular along `r`) with off-diagonals the same
order as the diagonals whenever `r` has more than one nonzero component. `K` is
not diagonally dominant, so Gauss-Seidel over its three scalar rows has no
convergence guarantee. Order of magnitude: for a segment with `|r| = L/2` and
transverse `I ≈ mL²/12`, the angular term is `(L/2)²/(mL²/12) = 3/m`, i.e.
**three times** the linear term `1/m`. The coupling is not a correction, it
dominates the row.

**A 2x2 block for the friction pair at one contact point** (§1.5, §5 S7): the
two tangent rows couple through `I⁻¹` off-diagonals once lever arms exist, and
the Coulomb limit is a disc, not two boxes.

Practical form for the block: assemble `A[i][j] = J_i M⁻¹ J_jᵀ`, solve
`A λ = -J u` (bias excluded, see below), Gaussian elimination with partial
pivoting for `n <= 4`, ridge `A[i][i] += ε` to keep the factorization defined at
singularity. This is `4c92518:2480-2512` with `gg·inv_Ia` replaced by
`Ga_iᵀ I_a⁻¹ Ga_j`.

### 2.3 Composition with the campaign's existing laws

**The one door (INV-7).** Today: `inv_mass_momentum(p)`
(`src/core/physics_system_v4.cpp:483-489`) and `apply_pair_impulse(a, b, jx,
jy, jz)` (`:493-500`), equal-and-opposite by construction.

The answer to "angular twin or extended signature" is forced by §2.1: **one
extended signature taking a world application point.**

    apply_pair_impulse(Particle& a, Particle& b,
                       float Px, float Py, float Pz,
                       float jx, float jy, float jz);

Inside, the door computes `r_a = P - x_a`, `r_b = P - x_b` and applies both
`Δv` and `Δω` through the predicate. A twin door taking two independent arms
**cannot** make the equal-and-opposite guarantee for angular momentum, and
that guarantee is the entire reason the door exists. The linear-only overload
stays as `P` at the pair's midpoint or as an explicit no-torque call, whichever
reads better at the call sites.

The predicate stays **one question with two answers**: `inv_mass_momentum(p)`
plus `inv_inertia_momentum(p) -> Mat3` (zero matrix for KINEMATIC, sleeping,
massless). One predicate, two return types, so a body can never be immovable to
translation and free to spin, which is the exact inconsistency
`4c92518:2427-2438` had to fix by hand.

Note a live gap this closes: the linear impulse apply at
`src/core/physics_system_v4.cpp:3228-3236` does not call
`apply_pair_impulse`. It recomputes the door's arithmetic inline (with a
redundant `!pb.is_at_rest && solver_mode != KINEMATIC` guard at `:3232`). The
row conversion should route it through the door rather than growing a second
inline copy that also handles torque.

**Warm starts.** Contacts warm-start at `:2660-2700` and apply linear only. The
extended warm start must apply `λ n̂` **and** `λ I⁻¹(r x n̂)`. Store `λ` and
recompute `r` from live geometry at apply time rather than caching the arm:
the arm is geometry, geometry moves between frames, and INV-8 (`rows-live-sized`)
is precisely the law that a row must not apply impulses sized for a different
world. Caching `r` would reintroduce the disease at the warm-start door.

**INV-10 momentum-unit caps.** Today the impulse-memory cap is
`m_light * GLUON_MAX_BIAS_VELOCITY` (`:4098-4103`), a momentum, because
"an accumulated impulse is momentum" and the comparable quantity across masses
is the speed it imparts. A full-Jacobian row's λ imparts both `Δv` and `Δω`, so
the mass-uniform quantity generalizes to **the point-velocity delta at the
application point**:

    Δ(v + ω x r) along n̂, for body a  =  λ · [ 1/m_a + (r_a x n̂)ᵀ I_a⁻¹ (r_a x n̂) ]
                                       =  λ · K_a

    λ_cap = MAX_BIAS_VELOCITY / max(K_a, K_b)

This is derived, not declared (INV-9), and it subsumes the two convergence
metrics the solver currently keeps in incompatible units:
`max_dv_this_iter` (`:3208-3216`) and `max_domega_this_iter` (`:3053-3057`).
One honest metric replaces two.

**Shrink-only row-mass refresh (INV-8).** Today at `:2533-2569`. Two
requirements:

1. It must recompute the **full** `J M⁻¹ Jᵀ`, not `1/(inv_a + inv_b)`. As
   written, the gluon branch (`:2551-2557`) computes the linear-only value and
   overwrites `c.effective_mass` whenever that value is smaller. Since the
   built value includes the angular terms, "smaller" is reachable: a row built
   with `b` asleep has `eff = 1/(inv_a + ang_a)`; after `b` wakes,
   `1/(inv_a + inv_b)` is smaller than that whenever `inv_b > ang_a`. The row
   would then be **priced linear-only and spent with torque**, an INV-20
   violation. *This is inferred from the code as a reachable path, not
   observed in a run.*
2. Shrink-only still means "K may only grow". Waking a body grows both the
   linear and the angular contribution, so the direction is unchanged. **Never
   zero** stands: zeroing tore the 1046:1 ringing ladder (INV-8 origin;
   `tests/test_light_body_ringing.cpp:56,70`, light end 0.00238 kg).

**The manifold eff/N split.** The split's justification is a rank argument:
"N same-normal rows describe a rank-1 error; each solving it with FULL effective
mass over-corrects by xN per sweep"
(`include/logosphere/physics/physics_solver.h:97-103`). **The moment rows carry
lever arms, that argument no longer holds.** N points on the same normal but at
different `r` are N *independent* rows carrying genuine torque information, and
`eff/N` would under-correct each by N. The split must be re-derived or replaced
by a block over the manifold's rows. This is a hard dependency edge, not a
detail: it is why S5 exists as a slice of its own (§5).

**Sleeping and KINEMATIC for angular DOFs.** Covered by the one predicate
above, plus two writes outside the solver that must follow it: zero `omega_*`
alongside `vx/vy/vz` on entering rest (`:4643`), and honor the predicate in
`integrate_angular_velocities` (`:4749`, KINEMATIC-only today). Without both, a
row can correctly refuse to spin a sleeping body while the integrator spins it
anyway.

**Pipeline placement.** Per `docs/todo_plans/PHYSICS_PIPELINE_SEQUENCE.md`, the
new row type slots into nodes 8 (turtle rows), 9 (contact rows), 10 (gluon +
drive rows), and is solved at node 14. It must respect
`requires_before(row_mass_refresh -> warm_start_apply)` and the
`must_see_live_state_of(contact_row_build -> wake)` edge, whose repair is node
12's shrink-only refresh. No new node is required; nodes 12 and 13 change
content, not position.

### 2.4 The two instantiations

**A. The anchor / pivot row.** `P` = the bond's world attachment point, one
point for both bodies (§2.1). `n̂` = the axis being constrained, three rows for
a point anchor. `C_dot` measures relative velocity at the attachment.
The joint's three anchor rows plus its drive row form the 4x4 block (§2.2).

The drive row, as the block's fourth row, has `J = [0, n̂, 0, -n̂]` and

    K = n̂ᵀ I_a⁻¹ n̂ + n̂ᵀ I_b⁻¹ n̂

Note this differs from today's `effective_inertia = 1/(1/I_a + 1/I_b)` with
`I = GetInertiaAboutAxis(n̂)` (`:2262-2272`, `:1954-1963`): the correct form is
the sum of the *projections of the inverses*, not the inverse of the sum of
reciprocals of *projections*. §1.3's inequality again.

**B. The contact-with-lever-arm row.** `P` = `manifold.points[cp].px/py/pz`,
already computed (`include/logosphere/physics/contact_manifold.h:18-22`) and
today discarded after filling a `CollisionEvent`
(`src/core/physics_system_v4.cpp:1367-1369`). `n̂` = the manifold normal, sign
unchanged (`contact_manifold.h:26-35` documents that it points B toward A and
that the jacobians are written for that sign). `min_impulse = 0` stays: a
contact still only pushes.

This is what makes a box topple rather than slide off a ledge. A normal impulse
at a point offset from the centre of mass produces `Δω = I⁻¹(r x n̂)λ`, and the
sign of that torque comes from `r`, which comes from geometry. **No world-up
vector appears anywhere in §2.** The formulation works on walls, on ceilings
and in zero-g by construction (INV-6).

For the turtle row, `n̂ = (0,0,1)` is plane geometry, not a gravity assumption
(the existing comment at `:746-748` makes exactly this distinction), but `P`
must be the OBB's support point against the plane, which the turtle path does
not compute today (§1.2).

---

## 3. WHY SCALAR ROWS FAILED

The measured event: commit `a27e8f0` (2026-08-09), "Implemented naively it
BREAKS the cancellation (proving the diagnosis) but goes unstable: BENT tore and
flew at 99.6 m/s." Reverted the same commit; what remains in the tree is the
half-machinery of §1.1.

### 3.1 The one-sentence cause

**The naive fix added a coupling term to the row's measured RESIDUAL without
adding the matching term to its EFFECTIVE MASS.**

A row that measures a coupled residual and prices it with an uncoupled mass
over-corrects by exactly the ratio of the two, on every sweep. That is INV-20
(`size-and-spend-in-the-same-model`) stated for coupled DOFs, and it names the
failure without appealing to anything outside the invariant set.

Concretely. The pivot law's second clause is `dv = -(dω x r)`: an angular
impulse must move the body linearly so the anchor stays put. Adding that to the
angular solve creates a path from the drive row's `λ_ang` into the anchor rows'
`v_rel` (which already reads velocity at the anchor, `:3110-3125`). The anchor
row's `K` (`:1893-1913`) knows about `(r x J)²/I` -- force at the anchor making
torque -- but has no term for the reverse path, because the reverse path did not
exist when `K` was written. The angular row's `effective_inertia`
(`:1954-1963`, `:2262-2272`) has no linear term at all: `c_ang.jx = c_ang.jy =
c_ang.jz = 0` at `:1964`, "No linear Jacobian for angular".

So each of the two rows now feeds the other, and neither one's price includes
the feedback. Loop gain above one gives geometric divergence.

### 3.2 Why paralysis and explosion are the same defect

Before the naive fix: drive `+3.31 rad/s`, anchor `-3.31 rad/s`, net
`+0.001 rad/s`, "every solve, forever" (`a27e8f0`). Two rows perfectly opposed.
`e8ca0d9` isolated the cost with `ANCHOR_TORQUE_OFF=1`: true convergences
1623 -> 2691 (+66%), iteration budget exhaustion 226 -> 4 (-98%), and one shuffle
seed's violence 8.887 -> 0.218 m/s (40x). Its conclusion: "Perfectly opposed rows
cannot improve, so Gauss-Seidel stalls on them by construction."

After the naive fix: the same two rows, now with a positive feedback path, tore
a blade at 99.6 m/s.

These are one defect at two gains. Below the divergence threshold you get
paralysis (the blade that would not stand up). Above it you get a tear. The
naive fix raised the gain without changing the pricing, so it moved the system
across the threshold. That is why "it broke the cancellation" and "it went
unstable" are the same sentence.

The signature supports this. A geometric divergence tears in a handful of
sweeps; an additive error drifts. The engine has an independently measured
instance of the same shape on a different mechanism: rows sized before a wake
"overcorrect x1.96 per sweep -- a geometric divergence that exhausts all 32
iterations and lands +-100 m/s on the one drive child in the chain"
(`:2543-2547`). Same disease, different cause, same fingerprint.

### 3.3 Three scalar rows for one 3-DOF constraint

Independent of §3.1, the three gluon axis rows (`:1830-1919`) are three scalar
rows for what is one 3-DOF point constraint, each pricing itself with only its
own diagonal term `(r x ĵ_k)²/I`. The true effective mass is the 3x3 `K` of
§2.2, whose off-diagonals are the same order as its diagonals and whose angular
part is rank-2. Gauss-Seidel over the diagonal of a matrix that is not
diagonally dominant has no convergence guarantee, and the branch measured the
specific instance: "the 2x2 core of that matrix is nearly singular (determinant
under half a percent of its terms)" (`4c92518`).

This is why §2.2 puts the joint's rows in a block. It is a second, independent
reason the scalar formulation could not be rescued by better bookkeeping.

### 3.4 A third contributor, live today but not the cause of that event

The anchor-torque path prices and spends with `GetMomentOfInertia()`
(`:1898`, `:1908`, `:3245`, `:3253`, `:3265`), which understates a grass blade's
transverse inertia by 130x (`src/particle_core.h:264-271`). Every unit of anchor
force buys 130x the spin it should. The drive row, meanwhile, uses
`GetInertiaAboutAxis` (`:2266-2270`), the real value. Two rows acting on one
body's rotation with inertia models 130x apart is an INV-20 violation across
rows: each is internally consistent, the pair is not.

**Timeline, stated because it matters:** `GetInertiaAboutAxis` landed
2026-08-10 (`96e2688`), one day **after** the 99.6 m/s measurement
(`a27e8f0`, 2026-08-09). At that time both paths used the scalar, so this was
**not** a contributor to that specific event. It is a live hazard now, and any
revival of the anchor-torque path without S1 reintroduces the 130x mispricing
into a mechanism that is finally sensitive to it.

### 3.5 Why the full-Jacobian row cannot reproduce this

`K = J M⁻¹ Jᵀ` is computed from the **same J** that measures the residual. The
residual and the price are one object. Over-correction by a coupling the price
did not see is not expressible: any coupling present in `J` is present in `K`,
identically, by construction. §3.1's failure mode has no syntax in this
formulation.

For the coupled group, the block solves all rows simultaneously, so "two rows
correcting each other" has no referent: there is one solve. The near-singular
direction of §3.3 is absorbed by the factorization plus the ridge, rather than
iterated through.

**The full-Jacobian row has a different failure mode, and it is already
solved on this branch.** A direct solve reaches its Baumgarte target exactly,
and a target reached exactly is free kinetic energy (`1f5cdd4`: 1.081 m/s and
climbing on a body nobody was touching; `BLOCK_NO_BIAS=1` gave 0.0000). The fix
is to carry no position bias in the velocity phase, which is now the tree's
general law via universal split impulse (`60bc585`, `a50218a`; the split rules
at `:2960-2999`). **The block must be built on top of split impulse, never
beside it.** That is a design constraint, not a preference: `2862edd` proved
bias-in-block is unstable and `1f5cdd4` proved bias-out-of-block-without-a-
position-pass turns all four rungs red.

---

## 4. MIGRATION PATH

### 4.1 Conversion order, and why

Ordered by "how much new geometry does this row need", cheapest first, because
each conversion should test the row machinery and not the geometry.

| # | Row type | Site | New geometry needed | Notes |
|---|---|---|---|---|
| 1 | Box-box contact, kinematic partner | `:1267-1279` | **none** (`manifold.points[cp].px/py/pz` already computed, `:1367-1369`) | one movable body, so no block question; proves the row, the door and the tensor in the simplest possible instance |
| 2 | Box-box contact, both dynamic | same | none | brings in the eff/N split re-derivation (§2.3) |
| 3 | Turtle contact | `:783-791` | **yes**: OBB support point against the plane; today only a Z extent is computed (`:764-776`) | single-body row; `n̂ = (0,0,1)` stays as plane geometry |
| 4 | Gluon axis rows | `:1830-1919` | none (arms exist, but must collapse to **one** `P`, §2.1) | enters the block with #5 |
| 5 | Quat-drive angular rows | `:2262-2325`, `:1954-2010` | none | becomes the block's 4th row; `K` form corrected per §2.4 |
| 6 | Friction rows | `:3314-3388` | orthonormal basis from the true normal | 2x2 block, disc clamp, own `K` |

Every conversion lands behind a **positive default-off lever**, matching the
campaign's practice for new risky mechanisms (`44b1a25`: "kept, DEFAULT OFF").
The `_OFF` naming (`ANCHOR_TORQUE_OFF:1885`, `ANGULAR_SPLIT_OFF:2968`,
`SPLIT_OFF:1277`, `IMPULSE_MEMORY_OFF:539`, `SLEEP_LAW_OFF:4628`) is for
mechanisms that are already on; a new mechanism gets a positive enable until its
rung is green, then flips and keeps an `_OFF` lever for A/B.

### 4.2 The regression fence

Every slice re-runs all of these. Numbers are from the tree; the ones marked
`capture` have no recorded value in this repo and must be measured in S0 before
the first mechanism lands.

| Gate | Value | Source |
|---|---|---|
| foliage mean canopy drift | 0.0101 m | `docs/todo_plans/GENERATION_DEBT_LEDGER.md:261` |
| light-body ringing ladder | 1046:1 mass ratio, light end 0.00238 kg, no growth, no tear | `tests/invariants/LEDGER.md:30`; `tests/test_light_body_ringing.cpp:56,70` |
| walk gate (`walk_through_grass`) | 0 detonations, 0.30 m worst drift | `tests/invariants/LEDGER.md` (INV-29 update entry) |
| characterization | bit-identical vs pinned baseline `c64f3caf02622e3f` (INV-27) | `tests/invariants/LEDGER.md` |
| unified harness | 27/27 | `tests/invariants/LEDGER.md` |
| explosion detector | 0 speed events on every gate scene (INV-11) | `src/core/physics_system_v4.cpp`, always compiled in |
| energy ledger | `d_TOTAL == 0` on a settled scene (INV-3, INV-24) | `ENERGY_LEDGER=1` |
| INV-29 constants gate | ratchet holds; `KNOWN_RESIDUALS` table does not grow | `tests/test_inv29_constants_gate` |
| INV-15 owner blindness | count stays at the pinned seven, or **shrinks** | `tests/test_inv15_owner_blindness` |
| Eden headless FPS | `capture` | `build-release` + the bench kit, `docs/todo_plans/PERF_RESEARCH_KIT.md:201-223` |
| full headless sweep | `capture` (expected 257 pass / 15 fail / 10 skip per `LEDGER.md`) | `scripts/physics_sweep.py` |

Note on the characterization gate: **S1 will move it deliberately.** Correcting
a 130x inertia error is a behavior change by definition. The baseline is
re-pinned once, in its own commit, with the diff reviewed rather than rubber
stamped. Every other slice holds it bit-identical or explains itself.

Note on INV-15: three of the seven pinned owner-reads are in the anchor-torque
family (`1876`, `1886`, `3225`, `3236` are pinned by the test; `1895`, `1905`,
`3255`, `3268` are the same family's gates). The row conversion should
**shrink** that table, which the test is built to require in the same commit.
That makes task #43 partly a consequence of this campaign rather than a
competing one.

### 4.3 What dies

- **The two-rows-correcting-each-other pattern.** The anchor row's partial `K`
  (`:1893-1913`) and the drive row's independent `effective_inertia`
  (`:1954-1963`, `:2262-2272`) both dissolve into the block's `A`.
- **`apply_anchor_torque`** (bool, `physics_solver.h:176`) and the six
  `anchor_r*` floats (`:190-191`), replaced by one application point per row.
  `a27e8f0`'s own commit message already called this: the pivot law "deletes the
  apply_anchor_torque special case rather than adding to it".
- **`ANCHOR_TORQUE_OFF`** (`:1885`). Absorbed. Its RCA value was to prove the
  opposed-pair stall by removing one half; once the pair is one row there is no
  half to remove. Deleted with the pattern, per the "replacing a mechanism ->
  delete the old code entirely" rule.
- **`pivot_inertia_a/b`** (`physics_solver.h:188-189`). Dead today, and the
  correct formulation never forms that quantity (§2.1). Deleted.
- **`effective_inertia` and the axis-constrained `Δω` apply** (`:3033-3045`),
  superseded by `Δω = I⁻¹ L` through the door.
- **The inline linear-impulse apply** (`:3228-3236`), replaced by a door call.
- **The per-axis `MAX_OMEGA` clamp** (`:4788-4789`, `:4810-4813`) becomes a
  magnitude clamp (§1.4), or is retired if the row caps make it redundant.
- **`ANGULAR_DRAG` as an unconditional per-substep sink** (`:4790`,
  `:4814-4815`): owner ruling required (§6.2).
- **The two-box Coulomb clamp** (`:3347-3349`, `:3373-3375`) becomes a disc.

---

## 5. SLICE SEQUENCE

Dependency-ordered. No estimates: the ordering is the deliverable. Each slice
names what it lands, what proves it, and what has to be rebuilt if it is done
out of order.

---

**S0. Baseline and ground truth.** No mechanism.

Capture every `capture` row of §4.2. Resolve the rotation ladder's actual
runtime verdict: `TEST_AUDIT.jsonl:120`, the test's own header
(`tests/test_rotation_ladder.cpp:11-18`) and commit `342fc2a` give three
different answers, and the recorded sweep row (`FAIL, rc=1, 0.9 s`) is too short
to be a real run of ~2500 frames. Also fix the rung-1 print/gate mismatch
(`tests/test_rotation_ladder.cpp:438` gates at 0.15, `:443` prints "need <
0.10") so the acceptance ladder does not lie about its own thresholds.

*Out of order:* every later slice's "did this regress" question has no answer,
and the campaign's acceptance test reports a verdict nobody has verified.

---

**S1. ONE INERTIA.** Mechanism: world-space inverse inertia tensor.

Add one tensor function on `Particle`, derived from extents and orientation
(no new stored state). Re-express `GetMomentOfInertia`
(`src/particle_core.h:241-259`) and `GetInertiaAboutAxis` (`:276-312`) as
consumers of it so the four models of §1.3 collapse to one. This is the INV-28
principle applied to inertia: shared physical definitions are shared code.

*Proves:* a new headless test asserting `n̂ᵀI⁻¹n̂ != 1/(n̂ᵀIn̂)` for the blade and
`==` for the sphere; free-body integration produces a Dzhanibekov flip for an
asymmetric body (this is the cheapest existence proof that the tensor is a
tensor). Characterization baseline moves once, reviewed, re-pinned in its own
commit.

*Out of order:* every later `K` is wrong by up to 130x
(`src/particle_core.h:264-271`), so every gate tuned against it is tuned against
noise and must be re-tuned after S1 lands. This is the single largest numerical
error in the rotation path.

*Constants:* none expected. If the tensor needs a degeneracy floor, it enters
`schema/physics.yaml` (INV-29, zero tolerance).

---

**S2. THE ANGULAR DOOR.** Mechanism: one predicate, one door, both DOF classes.

Extend `inv_mass_momentum` to a companion `inv_inertia_momentum -> Mat3` with
the *same* answer to the *same* question. Extend `apply_pair_impulse` to the
application-point signature (§2.3). Route the inline apply at `:3228-3236`
through it. Close the two non-solver angular writes: zero `omega_*` on entering
rest (`:4643`) and honor the predicate in `integrate_angular_velocities`
(`:4749`).

*Proves:* extend the INV-7 prover to angular; a test that a body spinning in
place with zero linear velocity either does not sleep or stops spinning when it
does (today it does both wrong, §0.5). Energy ledger neutral on a settled
scene (INV-3, INV-24).

*Out of order:* every row conversion writes `omega`. Converting rows first means
writing those sites twice, auditing the sleep gap twice, and shipping a window
in which rows correctly refuse to spin a sleeping body while the integrator
spins it anyway. `4c92518:2427-2438` already had to hand-fix this exact
inconsistency inside the block; doing it in the door instead means the block
inherits it.

---

**S3. THE ANGULAR SINKS.** Mechanism: `ANGULAR_DRAG` and `MAX_OMEGA`.

Rule on `ANGULAR_DRAG = 0.95`/substep against INV-19 (§6.2). Make `MAX_OMEGA`
a magnitude clamp rather than a per-axis one (§1.4). Constant-work and
code-work in separate commits, per INV-29's decree.

*Proves:* a free body given angular velocity in vacuum retains it (or dissipates
at a rate with a stated physical model); the clamp behaves identically under an
arbitrary world rotation of the whole scene.

*Out of order:* **this is the slice most likely to be skipped and most
expensive to skip.** A box cannot be shown to topple through an 18.5%-per-frame
angular-velocity sink. Land contact torque first, watch it not topple, and the
RCA goes into the row formulation, where there is nothing to find. The sink is
upstream of the acceptance criterion for S4 and S5.

---

**S4. THE ROW TYPE, one instantiation.** Mechanism: full-Jacobian contact row.

Introduce the row representation (application point + `n̂`), `K = J M⁻¹ Jᵀ`, the
extended apply, the extended warm start (recompute `r` live, §2.3), and the
INV-10 cap in point-velocity units. Convert exactly one row type: **box-box
contact against a KINEMATIC partner**. The contact point is already computed
(`:1367-1369`); one movable body means no block question and no manifold-split
question. Behind a positive default-off lever.

*Proves:* a new ladder rung. A box dropped on one corner of a kinematic tile
must topple, and must topple identically with the scene rotated to a wall, to a
ceiling, and with gravity zeroed (INV-6, no world-up in the row math). Plus
`test_rotated_box_contact`, the energy ledger, and the whole §4.2 fence.

*Out of order:* proving the row on a hard instance means debugging the row, the
block, the tensor and the manifold split at once. This is the instance with
exactly one unknown.

*Constants:* the ridge `ε` if a block appears here (it should not);
the point-velocity cap is derived, not declared.

---

**S5. CONTACT TORQUE, general.** Mechanism: box-box both-dynamic, plus turtle.

Convert the remaining contact rows. Two sub-decisions, both forced:

- **The eff/N manifold split must be re-derived** (§2.3). Its rank-1
  justification (`physics_solver.h:97-103`) is void once rows differ in `r`.
  Options: keep the split and accept slower toppling; block-solve the manifold;
  or split only the normal-space redundancy and leave the angular part whole.
  A/B against S4's single-row instance, which is why S4 comes first.
- **The turtle row needs an OBB support point** against the plane; today only a
  Z extent is computed (`:764-776`).

*Proves:* the toppling rung with two dynamic bodies; `test_settling_flat` (the
plate resting on 276 mm of air); `test_rotated_box_contact`; walk gate
unchanged at 0 detonations; foliage 0.0101 m; Eden FPS not regressed.

*Out of order:* the manifold-split decision has no A/B baseline without S4.

---

**S6. THE JOINT BLOCK.** Mechanism: `A λ = -J u` per joint, 4x4 direct solve.

Revive `4c92518:2400-2552` against the current tree, with its three known
defects fixed: real `I⁻¹` instead of isotropic `1/GetMomentOfInertia()` (S1);
immovability through the one predicate instead of hand-rolled `a_fixed/b_fixed`
(S2); and **no bias in the velocity phase**, which is now the tree's law rather
than a branch experiment (`60bc585`, `a50218a`, split rules at `:2960-2999`).
Deletes everything in §4.3.

*Proves:* **rotation ladder rung 3** (`tests/test_rotation_ladder.cpp:711-720`,
seven ANDed gates: contacts > 0; peak tip > 0.15 m; segment rotation > 15 deg;
shear < 60 deg; peak joint gap < 0.300 m; final tip < 0.15 m; settled shear
< 5 deg and settled gap < 0.025 m). **INV-16 flips `aspirational` -> `active`
with this as its prover**, and the ledger records it.

*Out of order:* `1f5cdd4` and `2862edd` already ran this experiment without
split impulse and without a derived angle-dependent restoring torque, and named
both absences as the reason the branch was not mergeable. Both have since
landed (§1.7). Reviving before S1-S3 rebuilds the two dead ends the branch
already paid for.

*Owner ruling required here:* **orientation truth** (§6.1). `I⁻¹` needs one `R`
per body, and today `R` comes from `rotation_q` for quat-driven bodies and from
the Euler triple otherwise (`:4838-4840`,
`include/logosphere/physics/narrow_phase.h:82-86`). The block is the first
mechanism that cannot proceed on two answers.

*Constants:* the singularity ridge `ε` (branch used `1e-9`) enters
`schema/physics.yaml` with its unit and its RCA. Any block-size or scope
threshold likewise.

---

**S7. FRICTION BASIS.** Mechanism: orthonormal tangents, 2x2 block, disc clamp.

Replace the axis-aligned picks (`:3314-3333`) with a basis built from the true
normal; give the tangent rows their own `K` instead of borrowing the normal
row's (`:3345`, `:3371`); replace the two box clamps with a disc.

*Proves:* a box sliding on an oblique face decelerates identically regardless of
the world orientation of the whole scene; `test_rotated_box_contact`; the walk
gate's snowplow case (INV-12).

*Ordering is an open question* (§6.4): the tangent-basis defect is independent
of the row work and is a cheap correctness win on its own, but every friction
row is rewritten here anyway.

---

**S8. ORIENTED NARROW PHASE for the remaining shapes.** Mechanism: sphere-vs-OBB,
ellipsoid.

`include/logosphere/physics/narrow_phase.h` today: OBB SAT exists for box-box
(`:95-99`); sphere-vs-box takes `const AABB6& box` (`:46-52`), so a sphere
resting on a *rotated* box gets an axis-aligned normal; ELLIPSOID "falls back to
its enclosing AABB for now (conservative over-reporting)" (`:10`). Sphere-sphere
is genuinely orientation-free and needs nothing.

*Proves:* INV-12 (`true-geometry-contacts`) extended past boxes; a sphere at
rest on a tilted box does not drift.

*Out of order:* S4/S5 give those shapes a lever arm computed from a wrong
contact point, which is worse than no torque: a wrong `P` produces a confidently
wrong torque. If S8 cannot precede S5, the row conversion must be gated by
shape until it does.

---

**S9. TORSION.** Mechanism: `K = G·J/L`, `G = E/(2(1+ν))`.

The owed sibling of the derived family (`src/materials.h:174-187`). Bending
landed at `:5017-5018`; torsion has no implementation anywhere.

*Proves:* a bonded chain twisted about its own long axis resists and returns;
extend `test_grass_natures`' rotation gate (`peak_rot > 0.26 rad` for BENT and
STRAIGHT, `tests/test_grass_natures.cpp:541`).

*Out of order:* torsion is a force law, not a solver mechanism. Before S6 its
torque has no row that can carry it correctly, so it produces a number nothing
consumes and a gate that cannot be met.

*Constants:* none new if `G` is derived from declared `E` and `ν`
(`src/materials.h:186-187` explicitly reserves that: declaring `G` "would be a
fourth place for the three to disagree"). Under INV-9 it must be derived.

---

**S10. GYROSCOPIC.** Decision slice (§6.3). Either model `ω x Iω` or scope it
out in writing, with the reason recorded in the ledger.

*Out of order:* if it is in scope it belongs immediately after S1 (it needs the
tensor and nothing else). If it is out of scope, the decision must still be
recorded before S6, because a reader of the block solver will otherwise assume
the omission is a bug.

---

**Acceptance ladder for the campaign as a whole.** These three tests are
`red-by-design` in `TEST_AUDIT.jsonl` (`:120`, `:56`, `:136`), all with
`proves: []` and `touches: ["INV-12", "INV-14"]`, and
`scripts/physics_sweep.py:161-162` treats a PASS on any of them as a
`MOLE-GREEN` requiring promotion in the audit and the ledger. Turning them green
is the campaign's definition of done, and each promotion is a ledger entry.

| Test | Gate the campaign must turn green | Slice |
|---|---|---|
| `test_rotation_ladder` rung 3 | seven gates at `tests/test_rotation_ladder.cpp:711-720` | S6 |
| `test_rotation_ladder` rung 4 | `bent_ok && springy_ok && bouncy_ok && breaks_ok && !ghost && unbroken_ok`, `:1140-1150` | S6 + S9 |
| `test_single_blade_contact` | `rotates = peak_shear < 15 deg && (peak_tip < 0.15 || peak_pose_rot > 10 deg)`, `:467-469` | S5 + S6 |
| `test_grass_natures` | `rotates_ok = peak_rot > 0.26 rad` for BENT and STRAIGHT, `:541`; `tight_ok` final anchor gap < 10 mm, `:548-550` | S6 |

---

## 6. OPEN QUESTIONS FOR THE OWNER

Options and evidence. No recommendation is hidden in the framing.

### 6.1 Orientation truth (the deferred ruling, now due at S6)

*The ruling was deferred* "until the rotation-ladder work forces the
representation question" (`tests/invariants/LEDGER.md:155-157`). S6 forces it:
`I_world⁻¹ = R diag(...) Rᵀ` needs exactly one `R` per body.

State today: `rotation_q` integrates from the full omega vector (`:4823-4833`);
the Euler triple is published from it **only for `is_quat_driven` bodies**
(`:4838-4840`); for everyone else `rotation_q` is "a stale by-product".
Consumers already branch on the flag (`narrow_phase.h:85-89`).

- **Option A: quaternion is truth, Euler is a published view for everyone.**
  Removes the branch from every consumer. Cost: every body pays the publish, and
  the Euler path's existing semantics (the CW-from-+Z convention documented in
  `CLAUDE.md`) must be proven identical through the quaternion for bodies that
  never had one. Risk concentrated in the humanoid yaw cascade, which writes
  `rotation_z` directly.
- **Option B: keep the split, make the flag a solver-visible contract.** Cheapest
  now. Cost: the branch is permanent and appears in every new consumer, and it
  is a per-body representation flag inside physics, which is the shape of thing
  INV-15 exists to prevent.
- **Option C: Euler is truth, quaternion is derived per use.** Rejects the
  3-axis work already landed (`:4808-4833`) and reintroduces gimbal issues in
  the tensor rotation. Listed for completeness.

*Evidence bearing on the choice:* `GetInertiaAboutAxis` already builds its
body-frame rotation from the Euler triple only (`src/particle_core.h:302-309`),
so today a quat-driven body's inertia projection and its OBB use different
orientation sources unless the publish has run. That is an existing latent
disagreement, and it is the concrete cost of Option B.

### 6.2 `ANGULAR_DRAG`: dissipation, or a numerical sink?

`ANGULAR_DRAG = 0.95` per substep (`src/generated/physics_constants.h:301`),
applied unconditionally at `:4790` and `:4814-4815`, four times a frame. Its
comment calls it "general air resistance for rotation" but it carries no area,
no fluid density, no relative velocity, and applies in vacuum to every body.
INV-19 admits damping "only where a real dissipation process is being modeled".

- **Option A: derive it.** Rotational drag on a body of known extents in a fluid
  of known density, acting on relative angular velocity. Consistent with INV-9
  and INV-19. Cost: a fluid model the engine does not have.
- **Option B: delete it** and let contact friction and bond damping be the only
  angular dissipation. Cost: unknown; free bodies will spin much longer, and
  some gate somewhere is probably leaning on this without saying so. That is
  exactly what S0's baseline is for.
- **Option C: keep it, declared as a modeling boundary** the way INV-21 keeps
  position bias: a sanctioned approximation with stated bounds. Cost: it is a
  0.95-per-substep sink that will suppress the tipping S4 and S5 exist to
  produce, so the bound has to be argued rather than asserted.

*This one is on the critical path* (§5, S3), which is why it is listed second.

### 6.3 Gyroscopic terms: model or scope out?

`ω x Iω` is unmodeled. `integrate_angular_velocities` is `ω += (τ/I)·dt` with no
cross term (`:4772-4815`).

Evidence from the engine's regimes:

- The term scales with `|ω|²` and with the *anisotropy* of `I`. Bodies here are
  extremely anisotropic: the blade at `src/particle_core.h:266` is 40 x 3 x 300
  mm, principal-moment ratios in the hundreds.
- `|ω|` is capped at `MAX_OMEGA = 6.28 rad/s`
  (`src/generated/physics_constants.h:250`), so the term is bounded.
- Mass regime spans 0.0002 kg blades (`:1622`) to strata tiles.
- Almost every rotating body in the engine is *constrained*: bonded chains,
  driven joints, contacts. Gyroscopic effects on a constrained body are largely
  absorbed by the constraint rows. The visible cases are free-flying debris and
  thrown objects.

- **Option A: model it.** Cost is one cross product per body per substep once
  S1 exists. Buys the tennis-racket flip on tumbling debris, which is a
  visible-realism item, not a stability item. Note it makes the angular
  integrator non-linear, which can itself destabilize at large `dt`; the usual
  treatment is an implicit or semi-implicit step for that term alone.
- **Option B: scope out, in writing.** Record in the ledger that it is
  deliberately absent, with the reason (constrained bodies dominate, `|ω|` is
  capped). Cost: nothing now; a future reader of the block solver would
  otherwise read the omission as a bug.
- **No evidence in the tree** shows a gyroscopic effect being missed. This study
  found none, and says so rather than manufacturing a case.

### 6.4 Does the friction-basis fix precede or follow the row work?

The axis-aligned tangent picks (`:3314-3333`) are wrong independently of torque.

- **Before (as its own slice):** it is a self-contained geometry fix with a
  clean proof (scene-rotation invariance), it removes a confound from every
  later contact measurement, and it serves INV-12's spirit today rather than
  later. Cost: the rows are rewritten again in S7 when they gain lever arms, a
  disc clamp and their own `K`.
- **After (as S7):** one rewrite instead of two, and the 2x2 block form is
  designed once with all its requirements known. Cost: every S4/S5 measurement
  carries a known-wrong tangent basis, so any oblique-contact anomaly has two
  candidate causes.

*Fact bearing on it:* the tangent rows also borrow the normal row's effective
mass (`:3345`, `:3371`) and clamp as two boxes rather than a disc
(`:3347-3349`, `:3373-3375`). Fixing only the basis leaves two of three defects
in place, so "before" is not as cheap as it first reads.

### 6.5 INV-16's wording

The invariant says the row's inertia "is the anchor-axis inertia (I_com +
m*r_perp^2)". The correct formulation never forms that quantity: the
parallel-axis term is implicit in `(r x n̂)ᵀ I⁻¹ (r x n̂)` (§2.1), and
`pivot_inertia_a/b` get deleted rather than populated.

- **Option A: reword** to state the behavior (a body constrained at an anchor
  rotates about that anchor; the row measures velocity at the anchor and
  conserves angular momentum about it) and let the mechanism field name the
  full-Jacobian row.
- **Option B: leave it** and let the mechanism field carry the discrepancy.

Either way the ledger records the change, and INV-16's `status` flips
`aspirational` -> `active` only when S6's prover exists.

### 6.6 Where torsion sits relative to rung 4

S9 places torsion after S6 on the argument that a force law needs a row that can
carry it. But rung 4's four natures
(`tests/test_rotation_ladder.cpp:762-767`) and `test_grass_natures`' three
(`tests/test_grass_natures.cpp:110-120`) are distinguished today by *declared*
`angular_stiffness` values, some of which are explicitly capped for substep
Nyquist reasons (`test_grass_natures.cpp:112-114`, "ang_k capped under ~0.5
N·m/rad"). Whether those natures should instead fall out of derived bending plus
derived torsion is a question about what the acceptance test is testing.

- **Option A: derive both, retune the natures from material.** Consistent with
  INV-9. Cost: the acceptance gates move, and they are the campaign's
  definition of done.
- **Option B: keep declared natures for the ladder, derive for the world.**
  Cost: two sources of truth for bond angular stiffness, which is the exact
  shape INV-9 was written against. The ledger already flags the sibling case:
  "physics_system.h GluonConstraint defaults angular_stiffness 100.0 /
  angular_damping 10.0 are DECLARED bond parameters; INV-9 says derive them."

---

## Appendix A. Citation index

Primary mechanism sites, `fix/physics-surgery` @ `09c0654`.

| Subject | Location |
|---|---|
| PIVOT LAW comment | `include/logosphere/physics/physics_solver.h:177-191` |
| `pivot_inertia_a/b` (dead) | `include/logosphere/physics/physics_solver.h:188-189` |
| `apply_anchor_torque` flag | `include/logosphere/physics/physics_solver.h:176` |
| `eff_mass_share` (manifold split) | `include/logosphere/physics/physics_solver.h:97-103` |
| Row Jacobian semantics | `include/logosphere/physics/physics_solver.h:57-89` |
| "not shippable, needs ONE row" | `src/core/physics_system_v4.cpp:3022-3028` |
| One predicate / one door | `src/core/physics_system_v4.cpp:466-500` |
| Turtle contact build | `src/core/physics_system_v4.cpp:740-800` |
| Contact row build + manifold split | `src/core/physics_system_v4.cpp:1232-1279` |
| Contact point, used only for events | `src/core/physics_system_v4.cpp:1367-1369` |
| Gluon eff mass (linear) | `src/core/physics_system_v4.cpp:1608-1623` |
| Wake-on-strain | `src/core/physics_system_v4.cpp:1757-1777` |
| Anchor lever arms + `ANCHOR_TORQUE_OFF` | `src/core/physics_system_v4.cpp:1881-1888` |
| Anchor-aware row `K` | `src/core/physics_system_v4.cpp:1893-1913` |
| Impulse memory apply | `src/core/physics_system_v4.cpp:1937-1948` |
| Angular row, no linear Jacobian | `src/core/physics_system_v4.cpp:1950-1965` |
| Quat-drive angular row build | `src/core/physics_system_v4.cpp:2262-2325` |
| Row mass refresh (shrink-only) | `src/core/physics_system_v4.cpp:2533-2569` |
| Warm start apply | `src/core/physics_system_v4.cpp:2660-2700` |
| Angular split-impulse rules | `src/core/physics_system_v4.cpp:2960-2999` |
| Angular row solve + apply | `src/core/physics_system_v4.cpp:3000-3060` |
| Anchor velocity term in `v_rel` | `src/core/physics_system_v4.cpp:3110-3125` |
| Linear impulse apply (inline, not via door) | `src/core/physics_system_v4.cpp:3228-3236` |
| Anchor torque apply | `src/core/physics_system_v4.cpp:3240-3275` |
| Friction tangents (axis-aligned) | `src/core/physics_system_v4.cpp:3314-3333` |
| Friction rows borrow normal `eff` | `src/core/physics_system_v4.cpp:3345`, `:3371` |
| Coulomb two-box clamp | `src/core/physics_system_v4.cpp:3347-3349`, `:3373-3375` |
| Impulse-memory momentum cap | `src/core/physics_system_v4.cpp:4071-4104` |
| `update_rest_state` (no omega zero) | `src/core/physics_system_v4.cpp:4611-4652` |
| `integrate_angular_velocities` | `src/core/physics_system_v4.cpp:4743-4841` |
| Euler publish gated on `is_quat_driven` | `src/core/physics_system_v4.cpp:4838-4840` |
| Derived axial + bending force laws | `src/core/physics_system_v4.cpp:5006-5024` |
| Scalar inertia + its 130x comment | `src/particle_core.h:241-271` |
| `GetInertiaAboutAxis` | `src/particle_core.h:276-312` |
| Derived-stiffness family incl. torsion | `src/materials.h:174-187` |
| Narrow-phase shape coverage | `include/logosphere/physics/narrow_phase.h:1-14, 37-56, 95-99` |
| Contact point struct | `include/logosphere/physics/contact_manifold.h:18-35` |
| Substep node order | `docs/todo_plans/PHYSICS_PIPELINE_SEQUENCE.md` |
| INV-16 | `tests/invariants/INVARIANTS.jsonl:16` |
| Deferred orientation ruling | `tests/invariants/LEDGER.md:155-157` |
| Block solver | `feat/joint-block-solver` `4c92518`, `1f5cdd4`, `2862edd` |
| Pivot RCA chain | `a27e8f0`, `e8ca0d9` |

**INV-16, verbatim** (`tests/invariants/INVARIANTS.jsonl:16`):

> "A body constrained at an anchor rotates about that anchor, not about its
> centre: the row's inertia is the anchor-axis inertia (I_com + m*r_perp^2) and
> every angular impulse carries the coupled linear velocity so the anchor point
> does not move."

Mechanism field: "NOT YET SHIPPABLE: needs one row carrying a full Jacobian
(independent scalar rows measured unstable: blade tore at 99.6 m/s)."
Origin: "rotation-ladder rung 3: +3.3 rad/s of drive against -3.3 of anchor
correction, net 0.001, forever (the blade that would not stand up)."

## Appendix B. Constants expected to enter `schema/physics.yaml`

INV-29 is zero-tolerance: every declared constant is an engine input with a
value, a UCUM unit, a group and a doc string, in `schema/physics.yaml`
(`constants_registry` subset), generated into
`src/generated/physics_constants.h`. Constant-work and code-work are separate
commits by decree.

| Constant | Slice | Group (proposed) | Note |
|---|---|---|---|
| block singularity ridge | S6 | new `BlockSolve` | branch used `1e-9`; needs a unit and an RCA, or a derivation from `A`'s trace |
| max block size | S6 | `BlockSolve` | 4 today (3 anchor + 1 drive); a declared bound, not a magic literal |
| inertia degeneracy floor | S1 | `RotationalDynamics` | only if the tensor needs one; prefer refusing loud, per `:4759-4770`'s precedent |
| angular drag replacement | S3 | existing `RestSleepDamping` | value or deletion depends on §6.2 |
| `MAX_OMEGA` semantics | S3 | existing `GravityAndDrag` | value unchanged, meaning changes from per-axis to magnitude; doc must say so |

**Derived, therefore NOT registry entries** (INV-9): the point-velocity impulse
cap (from `MAX_BIAS_VELOCITY` and `K`), torsion `K = G·J/L`, and `G =
E/(2(1+ν))`.
