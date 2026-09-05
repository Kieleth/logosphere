# Night journal, 2026-09-04 — one INV-test at a time

Owner's orders at bedtime: "continue iterating in loop one change at
the time and rerunning, till we find more elegant ways to improve
malleus, I want RCA every run, understanding, deep, even if it hurts,
and finding novel ways to continue, log/journal, have fun, and remember
less is more, many times is about stepping back, reducing complexity
and finding a more elegant unified way to achieve things. one INV-test
at the time."

Protocol per run: TARGET (one INV-test) / HYPOTHESIS (one sentence,
falsifiable) / CHANGE (exactly one, behind a lever if it may not ship)
/ RUN (what was run, alone if timing matters) / NUMBERS / RCA (what
the numbers prove, what they refute, what they cannot say) / VERDICT
(keep, park behind a lever, or undo - never silently) / NEXT.
Branch: night/2026-09-04, stacked on fix/born-with-material. No merges,
no windows, sweeps alone before anything is called shippable.

## 0. Scope, then the night's theme

Owner, 2026-09-04 (awake a moment longer): "Malleus is just for the
ontology work, you can focus on physics here for now." So: physics
only, the INV registry as the gate, one INV-test per iteration.

The seam family is one mechanism with four names

Open in the registry and all touching tile seams: test_falling_cube
(INV-2/INV-12, 'contact telemetry records only vertical-normal
contacts'), test_tile_sticking and test_body_coherence
('single:seam-normals'), G-73 (a touching side row carries 15 N s with
no approach), and two shipped mitigations: the surface-continuity merge
(now with its precondition) and the opt-in face-has-area rule (+250 ms
per Eden steady frame). If one principled rule in the AABB narrow phase
retires the merge and the face rule and turns the seam tests green, that
is the elegant unification the owner asked for. If not, the RCA says
why. Start with the simplest body on the simplest floor.

## 1. test_falling_cube (INV-2 / INV-12): the instrument, not the physics

TARGET. test_falling_cube, known-open 'single:contact-telemetry'. It
read: physics PASS (cube stopped, vz 0.000, on floor at pen -0.001)
and telemetry FAIL twice ('Contacts recorded (0)', 'All normals
vertical (0 horizontal)' - the second fails only because the first is
empty).

EVIDENCE BEFORE HYPOTHESIS. Canary on the cube, frames 0-300: one
manifold per frame from f142 to ~f220 (P0<->P1, n=(0,0,+-1)), depth
-41.3 mm at f142, -72.7, -77.8 (the 80 mm margin catches it early),
then -35, -11.5, -3.7, -0.71, -0.23, -0.075 mm - NEGATIVE throughout:
the speculative rows slow it and it settles 75 um ABOVE the floor.
Asleep from ~f221, and the broad phase skips a sleeper, so no pair
after that. The telemetry snapshot copied collision_events_, which
carries only penetrating manifolds. A body that never penetrates never
appears. 300 frames, 0 contacts, honest and useless.

HYPOTHESIS. The row the solver priced IS the contact; record the rows
after the solve, with their accumulated impulses, and the cube reads
one vertical row per frame with a positive impulse while it rests, and
none while it sleeps. The seam family (tile_sticking, body_coherence,
G-73) becomes a question about horizontal IMPULSE on a flat floor,
which is what a seam phantom is - a force, not a normal's existence.

CHANGE (one). physics_system_v4.cpp: the telemetry block moved from
after contact generation to after the solve loop; snapshots built from
`constraints` (contact rows, not angular, not gluons): normal = -j,
signed penetration, normal_impulse, tangent_impulse, is_turtle.
entity_telemetry.h: those three fields on ContactSnapshot and a
horizontal_impulse aggregate on the frame. No test changed yet.

RUN. build-release/logosphere-tests --test test_falling_cube, then
test_tile_sticking and test_body_coherence for the family's numbers
under the new instrument (their bars untouched this iteration).

RUN 1a (provenance: NOT the change). The patch script's exact-text
anchor for the old telemetry block did not match; the script stopped
after writing the header, the build rebuilt against the new header, and
the three tests ran on the OLD instrument. Read as the baseline, and
worth having: test_falling_cube 0 contacts; test_tile_sticking 3389
horizontal rows recorded, 23 of them against floor tiles, 4.8 m walked,
4 sticking frames; test_body_coherence 29 horizontal floor contacts,
max separation 423.8 mm. Lesson for the loop: a run's provenance is part
of its RCA - a script that stops half-way produces a baseline wearing
the change's name. The block is now spliced by located line range.

RUN 1b (the change). test_falling_cube GREEN: 103 frames recorded, 248
rows, worst |normal_z| 1.0000, 0 horizontal, on floor at -0.7 mm. The
rows are per manifold point (up to four), so 'contacts' counts rows
now, not manifolds. test_tile_sticking: 10826 horizontal rows recorded
(was 3389: speculative side rows between Eva's own boxes and the
floor's neighbours are rows too), 26 against floor tiles (was 23),
4.8 m walked, 4 sticking frames. test_body_coherence: 29 horizontal
floor rows (unchanged), max separation 423.8 mm.

RCA. The cube's red was the instrument and nothing else: the physics
had been right all along (settled 75 um above its floor in the
cushion). The family's red is real and unchanged by the instrument: a
walking humanoid on a flat tiled floor gets 26-29 rows whose normals
are horizontal. What the new instrument adds is the question the old
one could not ask: whether those rows carry impulse. A horizontal row
with zero impulse is bookkeeping; one with impulse is the seam catch
(G-73's 15 N s was one). That is iteration 2's measurement, on
test_tile_sticking. Completeness gap found: the block runs after the
solve, so a frame with no rows for a tracked body is not recorded
('103 frames' for a 300-frame run); 'no contact' is an observation
and must be a frame. Fixed in this iteration before the commit.

VERDICT. Keep. The instrument change ships with the cube green and the
audit row updated; the two family reds stay red with their numbers.

RUN 1c (completeness). '103 frames' was neither a scope nor the ring
buffer: `if (constraints.empty()) return;` at the head of the solve
section leaves solve_contacts_v3 before its tail on every frame with no
rows, so the falling cube (f5-f141, nothing within 80 mm) and the
sleeping cube (f221 on) were never recorded - a probe at the block
counted 46 calls over 147 frames. Three greps missed it because
`grep -v "return [a-z_]*;"` also drops a bare `return;`. The recording
is now one helper, record_row_telemetry, called at both exits; a frame
with no rows is recorded empty, because 'no contact' is an
observation. Expected: 300 frames for the cube, numbers otherwise
unchanged.

RUN 1d (300 frames, 0 rows - the ring). With the empty-frame exit
recording too, the cube read 300 frames and ZERO rows. Probes at record
time: at physics call 142 the cube's snapshot holds 4 rows, the buffer's
newest holds 4, the buffer holds 142. So the rows were recorded and then
overwritten: solve_contacts_v3 runs once per SUBSTEP, the helper records
on every call, and a 300-update run writes N x 300 records into a ring
of 300 - what survives is the sleeping tail. Run 1b's '103 frames' was
the same ring holding only row-frames (no empty ones were written, so
nothing overwrote them). The old instrument had the same defect and
nobody could see it, because every frame was empty anyway. The
instrument must speak in FRAMES: record on the last substep only.

RUN 1e (the frame gate). SOLVER_SUBSTEPS is 4; the helper now records
on the last substep only and stamps the update as the frame.
test_falling_cube GREEN: 150 physics frames (the engine steps physics
at half its update rate in this harness), 60 rows, worst |normal_z|
1.0000, 0 horizontal. test_tile_sticking: 3446 horizontal rows per
frame, 20 against floor tiles, 5.6 m walked, 2 sticking frames.
test_body_coherence: 12 horizontal floor rows, 423.8 mm max
separation. The per-substep inflation is gone (10826 -> 3446, 29 ->
12); the reds are the same reds.

FINDING (not fixed, one thing at a time): test_tile_sticking walked
4.8 m in two earlier runs and 5.6 m in this one with no physics change
between them; it renders with GPU sync, so its walk is paced by the
wall clock and its counts move with it. A gate cannot be paced by the
clock. Booked in its audit row; the next iteration measures impulse on
it anyway, because the question is right even when the walk is noisy.

VERDICT 1. KEEP. Committed as one change: the telemetry reads the
solved rows, after the solve, once per frame, with impulses.

## 2. test_tile_sticking (INV-12): do the horizontal floor rows carry force?

TARGET. test_tile_sticking, known-open 'single:seam-normals': 20
horizontal rows between Eva and floor tiles per run (3446 horizontal
rows in all, most of them Eva's own boxes against each other).

HYPOTHESIS. A horizontal row on a flat floor is a seam catch only if it
carries impulse; the rows the count sees are mostly speculative side
rows at zero impulse, and the few that carry any are the sticking. If
the max impulse over the 20 rows is below ABSOLUTE_THRESHOLD (0.01 N s,
the solver's own 'an impulse under this is nothing'), the count law is
counting bookkeeping and INV-12's statement should speak of force. If
it is above, the heaviest row's frame, pair and depth are the next
canary.

CHANGE (one). The test sums and maxes |normal_impulse| over the floor
rows it counts, prints the heaviest three (frame, pair, normal, depth,
impulse, friction), and adds one law-tagged line: horizontal floor rows
carry no impulse (max < ABSOLUTE_THRESHOLD). The count line stays as
it is, so the two laws can disagree in the open.

RUN 2 (twice, identical). 20 horizontal floor rows; their impulses:
one row carries 1.27716 N s, the other nineteen sum to 0.00663 N s
(the second heaviest 0.00663, the third 0.00000). The count law
counts nineteen rows of bookkeeping and one catch. New line red:
'INV-12 horizontal floor rows carry no impulse (max 1.27716 < 0.01)'.

RCA (canary P11, physics frames 137-141; telemetry frame 35 is physics
frame 140, four substeps). The catch: P11 is a 0.07 x 0.06 x 0.06 m
foot part pitched -0.41 rad, at z 0.14 (bottom 0.11) over tile P2
(4 x 2 x 0.10 m, top 0.10, edge at y = 3.00). Frame 140: the pair goes
through the ORIENTED narrow phase and returns n = (0, -0.92, 0.40),
one point at (-0.13, 2.99, 0.10), corner = 1, depth -20.21 mm. Frame
141: the same pair returns n = (0, 0, 1), three points, depth -11 mm -
a face contact. So for one frame, while the swinging foot part crossed
the tile's edge 10 mm above its top, the SAT picked an edge-edge cross
axis (an oblique normal with the LARGEST separation, 20 mm, over the
face axis z with 10 mm) and the speculative row built on it fired
1.28 N s backward-and-up into the foot. A separated pair does not meet
on an edge-edge axis; it meets the face it is falling onto. This is
the OBB-path twin of the AABB metric's 'a gap beats a touch' (journal
entry 0): for SEPARATED pairs, 'least penetration' is not a rule for
choosing a normal at all.

VERDICT 2. KEEP the instrument line (red, with coordinates). The
count law and the force law now disagree in the open: nineteen inert
rows vs one catch. OWNER RULING owed on INV-12's wording (rows without
force are bookkeeping; the law should speak of force). Iteration 3
hunts the catch: in narrow_phase_obb, a separated pair chooses among
FACE axes only; cross axes are for penetration. Behind a lever.

## 3. The catch itself: a separated pair meets a face, not an edge

TARGET. test_tile_sticking's force line (max 1.27716 N s), and by the
same mechanism test_body_coherence's floor rows and G-73's family.

HYPOTHESIS. In narrow_phase_obb the edge-edge axis wins when its
overlap is clearly the smallest. For a PENETRATING pair that is the
separating-axis theorem doing its job. For a SEPARATED pair every
overlap is a negative separation and 'smallest' means 'most
separated', which is not a rule for choosing where two bodies will
touch: two boxes approaching from a distance meet on a face (a vertex
into a face, or face on face) unless they are skew enough to cross
edges, and a foot swinging over a tile is not that. Restrict a
separated pair to the six face axes; let the cross axes back in the
moment the face axes report penetration. Expected: the f140 row
becomes a z-face row, the 1.28 N s vanishes, the count law's 20 rows
stay (they are inert), the ladder's edge landings (which penetrate
before they matter) are untouched.

CHANGE (one). narrow_phase.cpp, narrow_phase_obb: use_edge also
requires best_face_overlap > 0. Lever SEAM_OBB_FACES_FIRST=0 restores
the old choice for A/B.

RUN 3. test_tile_sticking: the force line GREEN - max 0.00000 N s over
12 horizontal floor rows (was 1.27716 over 20), sum 0.00000, sticking
frames 0 (was 2), 5.8 m walked; the count line still red on 12 rows
that carry nothing. test_body_coherence: horizontal floor rows 12 -> 5
(no force line there yet). test_falling_cube green. A/B on the same
binaries with SEAM_OBB_FACES_FIRST=0/1: test_cube_drop_ladder 2
failures either way (R5/R6, G-66's wheel walk), test_jammed_sleep 17
either way (G-73's edge tiles), body_coherence 12 -> 5. The change
touches the seam family and nothing else that was measured.

RCA. The catch was the oriented SAT choosing 'the most separated
axis' for a separated pair. With cross axes reserved for penetration,
the foot part crossing the tile's edge gets the tile's face normal,
the speculative row along it does what a speculative row is for (slow
an approach to the face), and no impulse leaks sideways. The inert
rows that remain (12 and 5) are speculative side rows between Eva's
parts and neighbouring tiles at zero impulse: the count law counts
them, the force law does not care. Less is more here: no merge, no
face-area rule, one predicate on the axis choice.

VERDICT 3. KEEP behind SEAM_OBB_FACES_FIRST (default on). Sweep alone
before anything is called shippable. NEXT: body_coherence gets the
same force line (its count red is the same nineteen-inert-rows story
until measured); then G-73 with this lens - the AABB path's touching
side row that carried 15 N s is the same class (a separated or
touching pair given a non-face axis by a metric).

## 4. test_body_coherence (INV-12): the same question, the third witness

TARGET. test_body_coherence, known-open 'single:seam-normals': 5
horizontal floor rows per run after iteration 3 (12 before, 29 when
the instrument recorded per substep). Its count law is the same count
law; it has no force line.

HYPOTHESIS. The five rows are inert speculative side rows (max
impulse under ABSOLUTE_THRESHOLD), as tile_sticking's twelve are. If
one carries force, it is a second catch with coordinates and the next
canary; if none does, three witnesses agree that INV-12's count
wording counts bookkeeping.

CHANGE (one). The same force line as iteration 2: weigh the floor rows
by |normal_impulse|, print the heaviest three, assert the max under
ABSOLUTE_THRESHOLD; the count line stays. Written after the sweep for
iterations 1-3 finishes (no compiled edits while it runs).

RUN 3b (the sweep, alone): MOLES 44 (new-red 1, gone-green 0,
unaudited 43). The red: test_oscillation_diagnostic, final max speed
0.104858 m/s against a 0.1 bar. Alone, A/B on the same binary:
SEAM_OBB_FACES_FIRST=0 gives 0.093793, =1 gives 0.104858; on
fix/tree-bonds it read 0.056459, and on fix/born-with-material it
failed once under the sweep and passed alone three times. Read
plainly: a gluoned 2 x 2 brick stack on the turtle that never settles
below 0.09 m/s is not at rest, the bar 0.1 is where 'damping working'
was declared years ago, and the axis rule moved the residual 11 %.
Not a regression in kind; a standing oscillation that has been living
under a bar. VERDICT 3 stands (faces-first on); the diagnostic is
booked expect-fail with both numbers, and the oscillation is iteration
5's INV-test. Less is more would say: find why the stack jitters, not
where to put the bar.

RUN 4. test_body_coherence: 5 horizontal floor rows; four inert, one
carries 1.94480 N s (frame 65, P14<->P3, n = (0, 0.98, -0.21), depth
-3.8 mm). test_tile_sticking unchanged (12 rows, max 0.00000). So the
hypothesis was half right: a second catch, and it survived
faces-first, which means its axis IS a face - the moving part's own
tilted front face, against a tile it is passing. Canary before any
conclusion.

RCA 4 (canary P14, physics frame 260 = telemetry frame 65). P14 is a
foot part, 0.07 x 0.10 x 0.08 m, pitched -0.21 rad, centre z 0.15;
its lowest corner sits ~1 mm above tile P3's top (0.10), and its
front face is 3.81 mm short of the tile's edge plane at y = 5.00. The
oriented SAT returns n = (0, -0.98, 0.21) - the foot's OWN tilted
front face, corner = 1, one point at (0.08, 5.00, 0.10) - and the
speculative row on it fires 1.94 N s backward and up. Frame 261: the
same foot is over the tile with a clean face contact, n = (0, 0, -1),
four points. Faces-first let this through because the front face IS a
face axis, and among face axes 'most separated' picked it (3.81 mm)
over z (~1 mm at the corner).

THE RULE THE THREE SYMPTOMS SHARE. A speculative row (gap or touch)
along an axis is a prediction that the pair will meet along that
axis. The prediction is false whenever another axis still separates
the pair at that moment: the foot passes over the edge with 1 mm to
spare (here), the tile 2 mm above its diagonal neighbour never lands
on it (journal 0, the AABB gap-beats-touch), the tile hanging over
the crossed edges of two boxes meets a face first (iteration 3). One
predicate: a separated-or-touching row is valid only if EVERY OTHER
face axis overlaps by more than SLOP; a penetrating row is the
separating-axis theorem's business as before. Cross axes qualify only
when all six face axes overlap (a pair separated by its crossed edges
alone - the legitimate edge-edge case). This subsumes faces-first
(iteration 3) and the opt-in face-has-area rule (SEAM_FACE_AREA, whose
Eden cost came from applying the bar to PENETRATING rows too). Less is
more: one rule, one lever, both narrow phases.

VERDICT 4. KEEP the force line (red with coordinates); commit. NEXT:
iteration 5, the one rule in narrow_phase_obb first, measured on the
family, the ladder, the jammed twin; then the AABB path.

## 5. One predicate for speculative rows (oriented path first)

TARGET. The two force lines (tile_sticking max 0.00000 already under
faces-first, body_coherence max 1.94480), the ladder and the jammed
twin as the guard, the oscillation diagnostic as the borderline
witness.

HYPOTHESIS. Rule (journal 4): a separated-or-touching row is valid
only if every other face axis overlaps by more than SLOP; a cross axis
qualifies only when all six face axes overlap; penetrating pairs keep
the separating-axis theorem. body_coherence's 1.94 N s row has two
separating face axes (the foot's front, 3.81 mm; z at the corner,
~1 mm) and gets no row - the foot passes over the edge, as it did in
frame 261 anyway. tile_sticking's f140 row keeps its face (z is the
only loose face axis there once the cross axis is out). Nothing that
penetrates changes, so the ladder's edge landings and the jammed
twin's stacks read the same.

CHANGE (one). narrow_phase_obb: the six face overlaps kept; when the
minimum overlap over all axes is <= 0 the speculative predicate
decides the row (face, edge, or none); otherwise the old preference.
Lever SEAM_SPECULATIVE_FACE=0 restores the original SAT (the
faces-first lever of iteration 3 is subsumed and removed - its
behaviour is this rule's special case).

## 5b. The same predicate in the axis-aligned path

TARGET. G-73's born-red: test_jammed_sleep case G's four edge tiles
3 mm deep (17 red on the shipped default), the touching side row that
carried 15 N s with a 126 um hair of z-overlap; and the opt-in
face-has-area rule's 250 ms per Eden steady frame.

HYPOTHESIS. The opt-in rule was the right predicate applied to the
wrong rows: it barred PENETRATING rows whose footprint overlapped by
less than SLOP too, and a body pressed into a tile's corner region
lost its row (the cost, and possibly chatter at the bar). Applied to
gap-or-touch rows only - a separated-or-touching row along one axis
exists only if the other two overlap by more than SLOP - it removes
the diagonal support rows and the hair-thin side row (G green) while
every penetrating row keeps its normal from the metric as before. The
Eden bill should not return, because penetrating rows are untouched.

CHANGE (one). narrow_phase_aabb: the face-has-area block becomes the
speculative predicate, gated on `penetration <= 0`, under the same
lever SEAM_SPECULATIVE_FACE (default on); SEAM_FACE_AREA is subsumed
and removed. Measured on the jammed twin (G), the ladder, the seam
family, and Eden alone against the 621 / 872 ms steady tails.

RUN 5a. REFUTED. tile_sticking: 3 floor rows but max 1.35555 N s
(back from 0.00000), f65 P14<->P3 n = (0, 0.98, -0.22), gap 4.6 mm.
body_coherence: 6 rows, max 2.55474 (was 1.94480). falling cube green.
A/B on the guards (SEAM_SPECULATIVE_FACE 0 / 1): ladder 2 -> 4 failures
(R2 'spin SURVIVES flight' and R3 'torque-free flight conserves
angular momentum' turn red: a body with NO contact lost spin, so the
predicate built a row in free flight), jammed twin 17 -> 18 (case D's
admitted tile beside the stone moved at 0.041 m/s), oscillation
diagnostic 0.0938 -> 0.0464 (fewer speculative rows, less jitter in
the brick stack: a clue for that target, not a reason to keep this).

RCA. Canary at the walker's f260 under the predicate: the foot P14 is
0.88 mm INTO tile P2 (a clean four-point z contact) sliding at 2.0 m/s
in +y toward tile P3, 4.6 mm short of P3's edge plane. Against P3 the
z overlap is a hair over SLOP and the front-face axis is the one loose
face axis, so the predicate grants the front-face row - and it is
geometrically honest: a foot 0.9 mm below the top plane meets the next
tile's side as a 0.9 mm wall. The row is real; the WALL is the
artifact, and the artifact is the seam. That is what the surface-
continuity merge exists for (two tiles are one surface), and my own
precondition on the seam branch (footprint overlap > ADJ_EPS) switches
the merge off for exactly this crossing: 4.6 mm before the edge the
footprints do not overlap yet. The predicate also mis-handles free
flight (loose == 0 with a cross axis: an edge row for a tumbling cube
near a slab) - two loose axes is not 'no row' when one of them is
closing, and the right formulation is time-to-impact: the speculative
normal is the LAST face axis to close under the relative motion, and
no row when any loose axis never closes. That needs velocities the
narrow phase does not see today. Parked as a design, not as code.

VERDICT 5a. PARK. The predicate stays in the tree behind
SEAM_SPECULATIVE_FACE, default OFF, with these numbers; faces-first
(iteration 3) returns as the default under SEAM_OBB_FACES_FIRST. NEXT:
iteration 6 - the merge's precondition becomes 'the body rests on the
surface' (its bottom at the tile's top plane, and not a coplanar
sibling), which restores the merge for a foot crossing a seam and for
G's diagonal supports while keeping H's sibling phantom out.

## 6. The merge's premise, stated right: a body ON the surface

TARGET. The walker's seam catch (a foot 0.9 mm into tile P2 meeting
tile P3's side as a 0.9 mm wall), the same crossing in body_coherence,
and G-73's supports - all under one precondition for the
surface-continuity merge; H's sibling phantom must stay out.

HYPOTHESIS. The merge is right when the body is ON the surface the
neighbours form, wrong when the body is PART of it. Footprint overlap
with j alone (my precondition of 2026-09-02) says 'on j', which loses
the foot 4.6 mm before j's edge and G's diagonal supports. Stated
right: merge iff (a) the body's bottom is at j's top plane, within the
contact margin (resting, landing, embedded by error), (b) the body is
not a coplanar sibling of j (its z-extent differs), and (c) the body's
footprint overlaps the MERGED footprint (j with its coplanar sleeping
neighbours) by more than ADJ_EPS - it is somewhere on that surface.
H: the centre dirt tile rests on the thinner l11, which is not in the
l00 family's surface, and its footprint only touches that family's
footprint (overlap 0): no merge, no phantom lift. The foot: over P2,
which is in P3's family: merge, z contact, no wall. G's edge tile
landing: over b10, in b00's family, 2 mm above: merge, the diagonal
support rows return (G was green with them).

CHANGE (one). physics_system_v4.cpp: compute the merged box first,
then decide with (a)(b)(c) whether to use it. Same lever
SEAM_MERGE_PRECONDITION=0 (unconditional merge) for A/B. Measured on
the two force lines, the jammed twin (G and H), the ladder, the
oscillation diagnostic.

RUN 6. test_jammed_sleep 17 -> 1 failure (A/B: 5 with the merge
unconditional, 17 with the footprint precondition, 1 with 'on the
surface'). Case G fully green - the four edge tiles land at 0.3750
with their diagonal support rows back and no face-area rule. The one
red: H's centre dirt tile rests at 0.498 against 0.495 (bar 2 mm; it
was 0.516 with the merge unconditional, 0.495 with the footprint
precondition). tile_sticking unchanged (12 rows, max 0.00000).
body_coherence unchanged (5 rows, max 1.94480). Ladder 2, oscillation
0.104858, falling cube green - untouched.

RCA. H's 3 mm: the centre column's layer-2 tile is 5 mm thinner, and
5 mm is exactly COPLANAR_EPS, so l11 joins l10's merged family and the
merged plane sits at l10's top, 5 mm above d11's true support; the
solver lifts it 3 mm. That is the merge's own tolerance made visible:
'one plane within 5 mm' lifts a body on the lower tile by up to 5 mm,
against INV-2's 2 mm. The engine's own geometric-error bar is SLOP
(1 mm); Eden's tile families are generated at one z and the sleeper
judge lands them within SLOP. body_coherence's catch: the foot is a
ROTATED box, so its pairs go through the oriented narrow phase, and
the merge lives only in the axis-aligned branch (`!oriented_pair`). A
walking foot never sees a continuous surface. That is the structural
gap behind every remaining seam catch of a humanoid.

VERDICT 6. KEEP. Commit. NEXT: 7 - COPLANAR_EPS from 5 mm to SLOP in
the registry (schema/physics.yaml, regenerated), H as the assert, G and
the family as guards. Then 8 - the merge reaches the oriented path (an
unrotated sleeping tile widened by its family before the OBB SAT).

## 7. Coplanar means 'within the engine's own error': COPLANAR_EPS to SLOP

TARGET. H's 3 mm (test_jammed_sleep, one red), and the merge's
tolerance in general.

HYPOTHESIS. The merge treats tiles whose z-extents agree within 5 mm
as one plane; a body on the lower of two such tiles is lifted by the
difference, up to 5 mm, against INV-2's 2 mm. Tile families are
generated at one z and the sleeper judge lands them within SLOP, so
'coplanar' can mean 'within SLOP' without losing a real family, and
H's 5 mm step stops being one plane. G stays green (its layers are
exactly coplanar), the walker's numbers do not move (its tiles are one
z), the ladder and the diagnostic do not move.

CHANGE (one). schema/physics.yaml: COPLANAR_EPS 0.005 -> 0.001 (the
value of SLOP, named as such in the description); regenerated.

## 8. The surface reaches the oriented path

TARGET. body_coherence's catch (1.94480 N s, a pitched foot's front
face against the next tile 3.8 mm before its edge), and every seam a
walking humanoid meets: a foot is a rotated box, so its pairs take the
oriented narrow phase, and the merge lives only under `!oriented_pair`.

HYPOTHESIS. The tile under a foot is unrotated; its merged box (the
family's surface, judged as in iteration 6) can be handed to the
oriented SAT as an axis-aligned OBB in place of the tile's own. The
foot 0.9 mm into P2 approaching P3 then sees one surface, meets it on
z, and no side face exists to catch on. tile_sticking stays green, the
ladder (rotated cubes on ONE floor slab, no families) does not move,
G and H do not move (unrotated pairs keep the AABB branch).

CHANGE (one). physics_system_v4.cpp: build and judge the merged box
whenever j is an unrotated sleeping tile (rotated i included); in the
oriented branch, when j is unrotated, the OBB passed to the SAT is the
merged box's. Same lever SEAM_MERGE_PRECONDITION for the judgement.

RUN 7a (provenance: NOT the change). generate_ontology.py exited 2 and
the generated header still read 0.005; the build and every number that
followed were the iteration-6 baseline again (H 0.498, the rest
identical). Booked as such; the generator's failure is read before a
second attempt.

RUN 7b (the change; regenerated through the repo's conda env, 18
schemas). test_jammed_sleep still 1 red: H's centre tile at 0.497
against 0.495 (was 0.498 at 5 mm coplanarity, 0.516 unconditional).
Everything else identical: tile_sticking 12 / 0.00000, body_coherence
5 / 1.94480, ladder 2, diagnostic 0.104858, cube green. The constant
did what it should - l11 no longer joins the family 5 mm above it -
and a 2 mm lift remains from another source. Canary before guessing.

VERDICT 7. KEEP: coplanar now means within the engine's own error, the
registry carries the rationale, no test moved but the one it aimed at.
