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

RCA 7 (canary on H's centre tile, frame 1). Against its true support
l11 (top 0.445) the tile reads a 0.17 mm z-row - resting. Against the
four DIAGONAL layer-2 tiles (l00, l20, l02, l22, tops 0.450) it reads
z-rows of 5.17 mm each, and it is over none of them: they touch its
footprint at one corner. Each diagonal's coplanar family is an L (the
tile with one x-neighbour and one y-neighbour), the merge widens the
tile's box to the L's BOUNDING BOX, and that box covers the corner
cell where l11 sits - 5 mm lower. Judged 'on the surface' by footprint
overlap with the merged box, the L passes (c) while no member of the
L is under the tile. So the residual is not a tolerance; it is the
merge's geometry: the union of an L is not a rectangle, and handing
the SAT a rectangle over a hole lifts whatever stands in the hole.
Eden's compaction canary (a dirt tile that fell 7 mm, then read 94 mm
along z from a neighbour) is this exact shape: the fallen tile is the
hole in its neighbours' L.

## 8. The merged surface is a strip, never an L's bounding box

HYPOTHESIS. Widening along one axis at a time keeps the merged box a
rectangle that IS a union of tiles: the x-strip (j with its x-adjacent
y-aligned family) or the y-strip. A body is on the x-strip iff its
footprint overlaps the strip along x and overlaps j's own y-range; it
is on the y-strip symmetrically. Choose the strip the body overlaps
more. H: the centre tile overlaps neither strip of any diagonal
(their strips run away from it), so no diagonal row - it rests on l11
alone. G: the edge tile landing on b10 overlaps b00's x-strip
(b00-b10-b20) along x and b00's y-range? No - b00's y-range is
[-6,-2], the tile's is [-6,-2]: yes, it is over b10 which is in the
strip: the diagonal support row survives as a strip row. The foot
crossing P2 -> P3: on P3's x-strip? The tiles run along y there: the
y-strip P2-P3-P4, the foot overlaps it along y and P3's x-range: yes.

CHANGE (one). physics_system_v4.cpp: two strips built in the merge
loop, the on-surface test per strip, the chosen strip becomes aabb_j.
Same lever SEAM_MERGE_PRECONDITION=0 for the unconditional union.

RUN 8. test_jammed_sleep: EVERY CASE ANSWERS ITS LAW (0 failures) -
all eight cases green on the shipped default, no face-area rule: G's
edge tiles land exact with strip supports, H's centre tile rests on
l11 with no diagonal row. tile_sticking 12 / 0.00000, body_coherence
5 / 1.94480, ladder 2, diagnostic 0.104858, cube green: untouched.

RCA. The merge's defect was never its premise; it was its geometry.
'Two adjacent tiles are one surface' is true of a row of tiles and
false of the bounding box of an L. Three days of symptoms - the 95 mm
containment phantom, the diagonal supports, the touching side row that
carried 15 N s, Eden's 94 mm canary - were bodies standing in the hole
of an L's bounding box. A strip is a union of tiles; the bounding box
of an L is not. Less is more: the face-area rule (opt-in, 250 ms per
Eden frame) is no longer needed for anything measured tonight.

VERDICT 8. KEEP. G-69 closed on its stage; G-73's born-red answered by
mechanism (its 15 N s row was a strip-less union's phantom against the
diagonal bedrock; the exact row was not re-traced tonight, the stage
that carried it is green). Eden's compaction is the next thing to
measure with this merge, after the sweep.

## 9. The strip reaches the oriented path

TARGET. body_coherence's 1.94480 N s (a pitched foot's front face
against the next tile 3.8 mm before its edge), and every seam a
walking humanoid meets: a rotated foot's pairs take the oriented SAT,
which has seen only the tile's own box until now.

HYPOTHESIS. The strip (iteration 8) judged for any body over an
unrotated sleeping tile, handed to the oriented SAT as an axis-aligned
OBB, gives the foot one surface across P2 -> P3: z contact, no side
face, no catch. tile_sticking stays at 0.00000; G/H, the ladder (one
slab, no family), the diagnostic do not move.

CHANGE (one). physics_system_v4.cpp: the merge block judges for any i
when j is unrotated; the oriented call receives obb_of_aabb(aabb_j)
for an unrotated j. Lever SEAM_MERGE_ORIENTED=0 keeps the tile's own
box for A/B.

RUN 9. REFUTED (A/B on the same binary, SEAM_MERGE_ORIENTED 0 / 1).
body_coherence: 5 rows max 1.94480 -> 6 rows max 2.49020 (f35
P10<->P2, n = (0, 1, 0), a gap of 29.9 mm). tile_sticking: 12 rows max
0.00000 -> 6 rows max 2.36245, sticking frames 0 -> 1. jammed twin 0
either way, ladder 2 either way, diagnostic 0.104858 -> 0.105654.

RCA. The strip is a union of tiles in the axis-aligned path because
the AABB metric reads containment: a body inside the strip's footprint
gets z. The oriented SAT reads no containment - it takes the most
separated FACE axis (iteration 3) - and a strip has ends: a foot 30 mm
beyond a tile's y-edge, over the next tile, still within the x-strip's
x-range, sees the strip's y-end as its most separated face and gets a
wall 30 mm ahead. Against the tile alone that face lost to another
axis; against the strip it wins. The oriented path's speculative axis
is the question (journal 5a: the last face axis to close under the
relative motion), and until it is answered no box we hand it can be
'the surface'. The AABB path does not have this problem because
containment is a rule about being over something.

VERDICT 9. PARK behind SEAM_MERGE_ORIENTED (default OFF), code and
numbers kept. The night's real result stands at iteration 8. NEXT:
the sweep alone for iterations 6-8, then Eden alone: the steady tail
against 621 ms and whether the floor still compacts.

## Where the night stands (written while the sweep for 6-9 runs alone)

Shipped on night/2026-09-04, each its own commit, DCO, no merge:
  1. the contact telemetry records the solved rows, once per frame,
     with impulses (test_falling_cube green: its red was the instrument);
  2. tile sticking weighs its floor rows by force (19 inert, 1 catch);
  3. a separated pair meets a face in the oriented SAT (the catch: 0);
  4. body coherence weighs its rows (a second catch, named);
  6. the merge judged after it is built - a body ON the surface;
  7. coplanar means within SLOP (registry);
  8. the merged surface is a strip, never an L's bounding box -
     test_jammed_sleep all eight cases green, the face-area rule unused,
     G-69 closed on its stage, G-73 answered by mechanism.
Parked behind levers, default off, code and numbers kept:
  5a. one-predicate speculative rows (SEAM_SPECULATIVE_FACE=1): built a
      row in free flight, granted a real 0.9 mm wall;
  9.  the strip in the oriented path (SEAM_MERGE_ORIENTED=1): a strip's
      end is a wall to 'most separated face'.
Open, with coordinates: body_coherence's 1.94 N s (a pitched foot's
front face, oriented path); the count laws' wording (INV-12: count vs
force, owner ruling); the oscillation diagnostic on its bar (0.105 vs
0.100; 0.046 under 5a - the brick stack's jitter is fed by speculative
rows); the oriented path's speculative axis (the last face axis to
close under the relative motion) as the design the two parked
experiments point at.
Owner rulings owed, unchanged from the day: the face rule's default on
#171 (now unneeded on this branch's evidence), F-CROWN, A+B, refusal of
unnamed births, the Eden game-layer items, merge order #166..#173, and
whether this night branch becomes a PR on top of #173.

SWEEP (alone, after 9 parked): `SWEEP_VERDICT: MOLES 43 (new-red 0,
gone-green 0, unaudited 43)`. Iterations 6-8 pass the gate;
test_jammed_sleep's eight greens are audited expect-pass; the
diagnostic's bar crossing is booked. Eden alone is running.

## 10. The diagnostic on its bar: which body, and why (INV-31)

TARGET. test_oscillation_diagnostic, red at 0.104858 m/s against 0.1.
Its audit row says 'a gluoned 2x2 tile on the turtle'; headless it
runs PHASE 11 - a 64 x 64 floor, 16 trees, 32 rocks, 8 fallen logs -
and its verdict is the fastest body of that scene after ten seconds.
The row is corrected to the truth.

HYPOTHESIS. One body, not a stack, carries the 0.105 m/s: a tree
part on a born-strained bond (F-CROWN) or a rock on a seam. Naming it
turns a threshold into a mechanism. The rows-with-impulses telemetry
can then say what pushes it.

CHANGE (one). The test prints its three fastest bodies at the end
(id, speed, z, dims, material, asleep, bonds) alongside its verdict.
Built after Eden finishes, so the bench is not contaminated.

EDEN (alone, level-1 trace, 300 frames) on the shipped night state:
all-frames avg 813.5 ms (median 764, p90 1201, p99 1693); steady tail
avg 704.6 (median 706, p90 907, p99 1146). For the record: door 751,
sleeper 749, seam precondition-only 621, seam with the face rule
872/891. End state 9113 of 9301 asleep, 160 quiet, 59 dissatisfied -
the quietest end state measured (precondition-only: 9037 / 234 / 69;
face rule: 9104 / 162 / 66). 478 woken at start, 0 unnamed. The
+84 ms over the precondition-only tail is either the strip's extra
work per sleeping pair or run spread (the seam runs read 2 % apart);
an A/B with SEAM_MERGE_PRECONDITION=0 on this binary decides. The
compaction canary is not in the census; the end state is suggestive,
not proof.

RUN 10. The diagnostic's three fastest bodies at ten seconds: P4643
0.104858 m/s at (0.78, -12.81, 0.29), P4642 0.104701 at (0.73, -12.48,
0.30), P4641 0.104370 at (0.83, -12.06, 0.40) - stone, ~0.3 x 0.27 x
0.3 m, three bonds each, all awake, all at the same speed. Not a stack
jittering: a bonded stone cluster creeping as one body across the
tiled floor. The flip's born-red (test_physics_rock, a bonded rock
drifting 0.165 m; green since the rock was born STONE) is this class
on a seamless floor; phase 11's floor is 64 x 64 tiles.

EDEN A/B on the same binary, alone: SEAM_MERGE_PRECONDITION=0 (the old
union) steady 973.3 ms (median 928, p90 1365, p99 1718), all-frames
1173.7, end state 9089 asleep / 185 quiet / 75 dissatisfied; the strip
(default) steady 704.6, all-frames 813.5, 9113 / 160 / 59. The strip is
268 ms per steady frame cheaper than the union and ends quieter. The
+84 ms against the old 621 figure was a different binary, not the
strip's work.

VERDICT 10. KEEP the instrument (the three fastest bodies named). The
target is now a mechanism question: what pushes a bonded stone cluster
at a steady 0.105 m/s. NEXT: canary on P4643 at the end of the run -
rows, normals, impulses.

RUN 10b (canary on P4643, the last physics frame, 1200). One contact
row: P4643<->P418 (a floor tile), n = (0, 0, 1), depth -37.79 mm - the
stone hangs 37.8 mm ABOVE the floor. Position unchanged across frames
(0.2931 at the end, 0.2937 in the test's print); velocity -0.186 m/s
at the start of the solve, -0.105 after it, iters = 32. A stone held
off the floor by its three bonds to the two resting stones of its
cluster: gravity pulls it every frame, the bond rows pull it back, and
the solve leaves 0.105 m/s of unconverged velocity that the position
pass undoes. The diagnostic's 'oscillation' is a velocity residual on a
stiff bonded cantilever - stiff since the rock was born STONE (E 6e10;
as FLESH those bonds were a thousand times softer and converged, which
is why this number moved between branches and sits on the bar). This
is the class the joint block solver branch exists for (rows of one
joint solved simultaneously; open on rung 4), not a seam and not a
night fix: booked with its mechanism.

RUN 10c (level-2 trace, the whole run): 1199 of 1200 substeps exit by
iteration_budget_exhausted (32); the end-of-run dissatisfaction census
names stone rocks with 2-6 bonds held by gluon strain (P4546, P4547,
P4549, P4550, mat 4 = STONE). The scene never converges because its
rock clusters' stiff bonds never converge. VERDICT 10. Booked with the
mechanism; the joint block solver branch owns it. The instrument
(three fastest bodies) stays.

RCA 9 (canary P10, physics frames 138-142, the strip in the oriented
path on). Frame 140: `P1<->P10` (the tile P1 first - the broad phase
iterates awake bodies as i, so P1, the tile under the foot, is AWAKE)
z contact +3.44 mm; `P10<->P2` (the foot first: P2 asleep) n = (0, -1,
0), depth -29.89 mm: the foot at y 2.93 moving +y at 2 m/s sees P2's
y-edge at 3.00 as a wall. P2's family is built from SLEEPING coplanar
neighbours only, so P1 - awake because a foot is standing on it - is
excluded, the strip starts at P2's edge, and the seam between the tile
being stepped on and the next tile is exactly the strip's end. Every
catch a walker has produced tonight sits on that seam: the tile under
the foot is awake by construction. A tile woken by a step has not
moved; coplanarity within SLOP is the test of whether it is still the
floor, and it passes.

## 11. The family is coplanar, not asleep

CHANGE (one). The family loop no longer skips awake neighbours;
coplanarity within SLOP and adjacency decide membership. j itself must
still be a sleeping tile for the block to run (a foot over an awake
tile approaching a sleeping one is the case at hand). Measured on
body_coherence and tile_sticking with SEAM_MERGE_ORIENTED 0 and 1 -
iteration 9's reach may un-park if its wall was this - and the guards.

RUN 11 (A/B SEAM_MERGE_ORIENTED 0 / 1 on the same binary). Lever off:
unchanged (body_coherence 5 rows max 1.94480, tile_sticking 12 rows
max 0.00000, jammed 0, ladder 2, diagnostic 0.104858) - the walker's
catch lives in the oriented path and the family alone cannot reach
it. Lever on: body_coherence 5 rows max 0.00000 (the 1.94480 catch
GONE; the heaviest row is an inert tilted gap row, f36 P1<->P10,
impulse 0), tile_sticking 2 rows max 0.00000, sticking frames 0,
jammed 0, ladder 2, cube green; the diagnostic 0.104858 -> 0.156871
(the non-converging rock cluster's residual, already booked, moves with
any change to the rows its rotated stones see).

RCA. Iteration 9's wall was iteration 11's exclusion: with the awake
tile in the family, the strip under a walking foot has no end at the
seam, and the oriented SAT, handed that strip, reads the floor as one
surface. The two parked experiments were one defect seen from two
sides. Less is more: the reach is four lines, the family rule is one
line removed.

VERDICT 11. KEEP the family rule. 12: un-park the oriented reach
(SEAM_MERGE_ORIENTED default on; =0 kills), with the diagnostic's
0.157 recorded against it. Then the sweep alone, then Eden alone.

## 12. The oriented reach un-parked (default on)

CHANGE (one). SEAM_MERGE_ORIENTED default on; =0 kills. Verified to
reproduce run 11's lever-on numbers, then the sweep alone.

## 13. The angular drive owns its damping (INV-13) - after the sweep

TARGET. test_gluon_angular_drive_converges, known-open F7 since the
derived drag law replaced ANGULAR_DRAG: 'the PD drives leaned on the
dead constant as free world damping; the fix is the drive's own
damping term'. Two stone particles in space, one NailGluon with
angular_stiffness 200, angular_damping 12, target pi/4; must settle
within 0.5 s and hold without oscillation.

READ. The drive row (physics_system_v4.cpp ~3093) sets
angular_bias = ANGULAR_BETA * angle_diff / dt, clamped by
MAX_ANGULAR_BIAS_VELOCITY, with an unbounded impulse budget. Neither
angular_stiffness nor angular_damping enters the row: the 'PD' is a P
on the error at the velocity level, and its only damping is whatever
the world supplies - nothing, since G-42 killed the air-drag constant.

HYPOTHESIS. A velocity-level row that zeroes relative angular velocity
and then adds a bias proportional to the error is a P controller with
implicit unit damping per substep, and with ANGULAR_BETA it overshoots
and rings. The gluon's angular_damping (N m s / rad) belongs in the
row: a bias term -damping * relative_omega / effective_inertia (the
D of the PD), and the stiffness sets the P gain instead of the global
ANGULAR_BETA. One change: the drive row reads its own gluon's two
fields; ANGULAR_BETA stays for limits and dead zones. Measured on the
drive test (settle error, hold error), test_rotation_cascade_yaw and
the humanoid tests as guards.

RUN 12 (verified, default on): body_coherence max 0.00000 over 5 rows,
tile_sticking max 0.00000 over 2 rows. SWEEP alone: MOLES 44 (new-red
1, gone-green 0, unaudited 43). The red is a HANG: test_physics_minimal_v2
hit the 300 s deadline (rc None). A/B under a 60 s timeout, lever on
and off, before anything else.

RUN 12b (A/B under a perl alarm, no `timeout` on macOS).
test_physics_minimal_v2 is a 16 x 16 x 16 stack of 0.5 m tiles (4096)
plus a tree, frame-bounded at 1800 frames: lever off 18 s wall, all
4096 at rest, PASS; lever on killed at 120 s still stepping. Not a
hang - a cost, seven times or worse, that the walkers did not show
(their floors are one layer, their rotated bodies few). Tracing the
solve exits and row counts under both settings before deciding.

RUN 12c (level-1 trace, 60 s each). Lever off: the 4096-tile stack is
4096/4096 at rest by game frame 19 and the test exits (the 18 s). Lever
on: 58 physics frames in 60 s and 0/4096 at rest - the stack never
sleeps, so the frame-bounded loop runs its 1800 frames at ~1 s each:
the 'hang'. A stack of slightly rotated tiles over sleeping tiles,
each pair handed an 8 m strip, does not come to rest. The walkers
could not show this (one layer, few rotated bodies). The reach goes
back behind its lever, default off, with this evidence; the stack
under strips is the RCA to do before it ships.

RUN 12d (the gate census under the reach, level-2, frames 42-43): 202
dissatisfied, held by the contact-penetration gate (setter lines 2077
and 2081), and the named ones are tiles of the bottom two layers (P235
BRICK 0.5 x 0.5 x 0.1 at z 0.051 on the turtle, P508 at z 0.118), each
against a partner tile. Reading: a tile micro-rotated by settling has
a corner over its neighbour; against the neighbour's own box that
corner is outside the clipped face and reports nothing, against the
8 m strip it is inside the face and reports a 3-4 mm penetration; the
correction rotates the tile, the neighbour answers, the layer couples
and never sleeps. The strip tells a truth the raw box hid (the corner
IS over the neighbour, IS below its plane) and the solver cannot rest
on that truth at 32 iterations. Whether the corner's dip is real
geometry (a tilted tile) or the strip's plane (COPLANAR_EPS lets a
neighbour sit 1 mm lower) is the next canary, not tonight's.

SWEEP alone with the reach re-parked: MOLES 43 (new-red 0,
gone-green 0, unaudited 43). Shipped: 1-4, 6-8, 11. Parked with
numbers: 5a, 9/12. test_physics_minimal_v2 passes in 18 s.

READ 13b. The quaternion-target drive path (physics_system_v4.cpp
~3024-3045) already reads the gluon's own fields: bias = ANGULAR_BETA *
e / dt clamped, and for force-bounded bonds a torque BUDGET
tb = (angular_stiffness * |e| + angular_damping * |omega_rel|) * dt
bounding the row's impulse; rigid joints keep infinite authority. The
scalar-Z path the drive test exercises has the bias and an unbounded
budget, and reads neither field. So 'the drive's own damping term' may
already exist in one path and be missing from the other - the
asymmetry to measure first: the test's settle and hold numbers, then
the same budget on the scalar path as the one change, then the
humanoid tests as guards (their joints are scalar-Z drives).

EDEN on the shipped state (11 on, 12 off), alone: steady 1115.2 ms
(median 1079, p90 1391, p99 1818), all-frames 1075.3; end state 9027
asleep / 223 quiet / 79 dissatisfied. Before iteration 11: steady
704.6, all-frames 813.5, 9113 / 160 / 59. The family by coplanarity
costs 410 ms per steady frame and ends less quiet.

RCA. The merge widens per PAIR: a body over a family gets one
full-face row per member whose strip covers it (the diagonal supports
of case G are such rows). Sleeping members were the only ones
included; iteration 11 adds the awake ones, and in Eden the awake
tiles are exactly the ones under moving grass, trees and rocks - so
the duplicate rows land where the work is, and the extra support
over-corrects into jitter (more quiet-awake, more dissatisfied).
Alone, 11 moved no test; it served only the parked reach. It does not
pay.

VERDICT 11 (revised). PARK behind SEAM_FAMILY_AWAKE=1 (default off,
the sleeping-members family restored). Shipped: 1-4, 6-8. Parked with
numbers: 5a, 9/12, 11. A body over a family getting N rows for one
contact is the deeper defect under all three parked experiments: the
merge should produce ONE row per body per surface, which is a
different design (a surface as its own contact target) and the
morning's question, not the night's. Eden re-measured alone after the
park, then the sweep alone.

EDEN with 11 parked, alone: steady 1006.5 ms (median 989, p90 1216,
p99 1390), all-frames 1064.8; end state 9061 / 216 / 85. The same
physics read 704.6 ms steady earlier tonight. An hour and several
builds apart, the bench moves 40 % on its own (thermal state after a
build, whatever else the laptop does), and the 410 ms I charged to
iteration 11 was two binaries at two hours - NOT established. The
verdict's mechanism (one row per member) is a reading, not a
measurement. Rule for the rest of the night, and for the morning: an
Eden cost claim needs a PAIRED A/B, back-to-back, one binary, lever
off then on; absolute numbers across hours compare nothing. Running
that pair now for SEAM_FAMILY_AWAKE. SWEEP alone with 11 parked:
MOLES 43 (new-red 0, gone-green 0, unaudited 43) - clean.

PAIRED A/B, one binary, back-to-back, alone - SEAM_FAMILY_AWAKE:
off: steady 720.0 ms (median 723, p90 933, p99 1167), all-frames 836.8,
end state 9113 / 160 / 59. on: steady 1159.8 (median 1131, p90 1443,
p99 1887), all-frames 1112.8, end state 9027 / 223 / 79. The end
states are bit-identical to the earlier runs of each setting - the
physics is deterministic; only the wall clock drifted (705 / 720 /
1006 for the same physics). The cost of the coplanar family is 440 ms
per steady frame, measured. VERDICT 11 stands: parked, and now for a
reason that was measured rather than assumed.

RUN 13a (the measurement). test_gluon_angular_drive_converges: b_rot
0.0000 and rel 0.0000 at every printed frame, err 0.7854 (the whole
target) from f0 to the end; final rel 0.0000. And a_z falls 5.000 ->
0.716 over 55 frames: the 'two particles floating in space, no gravity
effect' scene is under full gravity. The drive is not under-damped; it
never engages. The booking ('the PD drives leaned on the dead drag
constant') describes a ringing row; there is no row. Reading the row's
build condition before anything else.

RUN 13b (canary on B, frames 1-3). The angular row IS built every
frame: ANGROW-Z P0<->P1 angle_diff -0.785398, bias -4 (ANGULAR_BETA
times the error over dt, clamped at MAX_ANGULAR_BIAS_VELOCITY),
effective inertia 0.00208, budget [-inf, inf], drive 1. And every
frame the solve exits after ONE iteration with the relative rotation
unchanged. A row that is built and never applied: the apply site must
be skipping it. Reading that gate.

RCA 13 (the apply gates, physics_system_v4.cpp ~4230 and ~4428, G-39's
rule). A contact row spins a body iff it is DYNAMIC; a NON-contact
row - a gluon's angular row - spins a body iff `is_quat_driven &&
owner == PHYSICS`, and measures its rotation on the same predicate.
The test's two boxes are neither quat-driven nor PHYSICS-owned (both
default false), so the drive's row is built, priced (bias -4, effI
0.00208) and never spent: omega stays (0,0,0), q stays identity, rel
stays 0.0000. Every live drive (entity_manager, humanoid_locomotion x4)
sets use_quat_target = true; the scalar-Z target the test uses has no
user but the test. The booking 'under-damped since the drag constant
died' described a ringing this test cannot have shown for as long as
its bodies stood outside the rotational-DOF world - the same stale-law
shape as test_particle_quat_euler_sync's note. VERDICT: the test is
rewritten to the ruled world (bodies quat-driven and PHYSICS-owned, a
quaternion target); its first honest numbers decide whether a damping
question exists at all.

CHANGE 13 (one, the test's). Both bodies quat-driven and PHYSICS-owned
(the rotational-DOF world's predicate), the gluon given a quaternion
target (pi/4 about z) alongside the scalar one, use_quat_target on -
the same three settings every live drive carries. Nothing in the
engine moves. The settle and hold numbers that follow are the drive's
first honest measurement since the flip.

RUN 13c (provenance: NOT the change). The patch's second anchor did
not match, the script stopped before writing, and the numbers are
bit-identical to 13a. Re-patched on single-line anchors.

RUN 13c (the change applied; provenance: the three settings grep at
lines 60/71/87 of the test). THE DRIVE ENGAGES.
  f0  rel  0.0000     a  0.0000  b  0.0000
  f5  rel -0.4000     a +0.2000  b -0.2000
  f10 rel -0.6667     a +0.3333  b -0.3333
  f15 rel -0.7830     a +0.3915  b -0.3915
  f20..f55 rel -0.7830, flat, forty frames.
  Final rel -0.7830 (target +0.7854). Max settle err 1.5684, max hold
  err 1.5684 (budget 0.0785). Both asserts red.

RCA 13c.
  1. Convergence: monotonic, met at frame 15 (0.25 s), zero overshoot,
     held flat for the whole hold phase. Magnitude 0.7830 against
     0.7854: 0.3 %. Equal inertias split the turn equally (+0.39/-0.39).
     There is NO damping question. The booking's story ("under-damped
     since ANGULAR_DRAG died") was never a measurement: the drive had
     not engaged since G-39's apply gate, and a flat zero was read as
     ringing by nobody, for a month.
  2. The sign. The engine's law (physics_system_v4.cpp:2779) is
     q_b = target * q_a, error q_err = q_b * q_a^-1 * target^-1, nulled.
     Standalone computation (scratchpad quat_rt.cpp, src/math/quat.h
     alone) with the MEASURED rotations:
       to_euler_zyx(from_axis_angle(+Z, +0.7854)).z = -0.7854
       q_b * q_a^-1 for the measured pair: euler z = -0.7830
       q_err = (w +1.0000, z -0.0012): identity.
     THE DRIVE IS MET. quat.h:296/305: to_euler_zyx outputs CW Z
     (`out_z = -atan2`), which is rotation_z's convention (CLAUDE.md:
     compass CW viewed from +Z); from_axis_angle is standard CCW;
     from_euler (quat.h:319-324) negates Z between the two. The test
     built its target with the CCW constructor and read the CW field
     expecting the same sign. Two conventions in one fixture.
  3. Live producers of target_relative_q: entity_manager.cpp:142 (rest
     pose = qb * qa^-1 measured), humanoid_locomotion.cpp:1269/2575/6365
     (composed quaternions), the plastic-yield site (FA(axis,-yield) *
     rel measured). All build the target in quaternion space from
     measured quaternions; none hand-builds a Z angle. The seam is the
     test's alone. No engine change.

CHANGE 13d (test-only, one line): the target through the engine's own
bridge, Quat::from_euler(0, 0, +pi/4), so target and reading share the
CW convention. Expected: rel +0.783, err 0.0024 < 0.039 (5 %), hold
err within 0.0785. Build + run in flight.

RUN 13d (provenance: from_euler at test line 91). GREEN.
  f5 rel +0.4000, f10 +0.6667, f15 +0.7830, flat through f55.
  Final rel +0.7830 (err 0.0024). Max settle err 0.7854 (frame 0's
  own), max hold err 0.0024 (budget 0.0785). Both asserts PASS.

VERDICT 13: KEEP (test-only, two changes of fixture, no engine change).
  INV-13's angular drive converges monotonically in 15 frames with no
  overshoot and holds. The residual 0.0024 rad (0.3 % of the target)
  is standing and flat: a stopping bar of the row, not a load (free
  flight, no torque on the pair). Named, not chased. The audit row's
  'under-damped since ANGULAR_DRAG died' is retired: it described a
  drive that had never engaged.
  Less-is-more note: the engine keeps two rotation conventions (CW
  rotation_z, CCW quaternions) bridged at exactly one place, quat.h's
  from_euler/to_euler_zyx. The class of error is a hand-built Z
  quaternion carrying a rotation_z-meaning angle. Census below.
  Census of the class (from_axis_angle about +Z outside quat.h): five
  engine sites bridge by hand with a minus each (physics_system_v4.cpp
  7776 `-parent.rotation_z`; humanoid_locomotion.cpp 2488 `-sign *
  target_z_rotation`, 5875 `-facing`, 5944 `-parent_p.rotation_z`,
  6355 `-cascade_dz`); test_reverse_leg_chain's five are joint-space.
  All five engine signs are right. Less-is-more candidate for the
  owner, not tonight: from_euler(0, 0, z) equals from_axis_angle(0, 0,
  1, -z) exactly (qx, qy identity), so the five hand negations could
  become one named bridge with zero behaviour change. Booked here only.

NEXT 14: test_particle_quat_euler_sync, the same seam's own test (its
  law 'B.rotation_z (legacy, must stay 0)' predates the flip).

## 14. test_particle_quat_euler_sync (touches INV-13): the stale law

TARGET: booked 'STALE LAW 2026-08-20: asserts the pre-flip split (legacy
rotation_z must stay 0); the quat-truth publish now writes it honestly.
Rewrite to the ruled world owed.'

BASELINE (14a, same binary as 13d): scene A green (quat-driven body,
euler (+0.1654,+0.1950,-0.3252) = to_euler_zyx(seed)); scene B red on
all three axes: the non-quat-driven body reads the SAME triple, the
test expects zeros. Max abs err 3.25e-01.

RCA: the ruled world, read from the code, not the booking.
  physics_system.h:626 `quat_truth_ = true; // DEFAULT ON, owner ruling
  2026-08-19`; physics_system_v4.cpp:271 LOGOSPHERE_QUAT_TRUTH=0 the
  kill switch; the publish (v4.cpp:6616) fires for `is_quat_driven ||
  (quat_truth_ && DYNAMIC)`: every DYNAMIC body publishes its Euler
  triple from rotation_q (G-23's frozen twin was the red that ruled
  it); KINEMATIC stays with its external writer (G-38: a KINEMATIC FK
  bone carries a stale quaternion; that exclusion is what still keeps
  is_quat_driven alive, PHYSICS_BOARD 351-366). Scene B's zeros were
  the law BEFORE the flip. The comment at the publish site itself still
  said 'Default OFF: bit-identical to the old gate' - a stale sentence
  at the very site the test proves.

CHANGE 14 (test + one comment): scene B keeps its body (DYNAMIC, not
quat-driven) and expects the published triple, same as A; scene C is a
KINEMATIC body with the same seed whose Euler must stay at zero (the
flip's exclusion, asserted for the first time here); the publish
site's comment says Default ON and names the kill switch. No engine
behaviour change. Expected: A, B, C green.

RUN 14b (provenance: 'test patched' + 'comment corrected' + the C line
in the output). GREEN.
  A (quat-driven, DYNAMIC): euler (+0.1654,+0.1950,-0.3252) = expected
  B (DYNAMIC, not quat-driven): the same triple, published
  C (KINEMATIC): euler (+0.0000,+0.0000,+0.0000), rot_q seeded, untouched
  Max abs err 0.00e+00. PASS.

VERDICT 14: KEEP (test + one comment, no engine behaviour change). The
  test now states the flip as it is ruled: every DYNAMIC body has one
  orientation whichever field a consumer reads; a KINEMATIC body's two
  ledgers belong to its writer (G-38's exclusion, asserted for the
  first time). The booking's 'rewrite owed' is paid. 14c: the B print
  label 'legacy' -> 'DYNAMIC' before the commit (rebuilt and rerun).
  Less-is-more note: two tests, one lesson. Both reds of the night's
  second half were laws written before a ruling (G-39's gate, the
  flip) and never re-read after it. A ruling that changes a law should
  visit the tests that state the old one on the day it lands; the
  audit's 'known_open' held the truth for a month while the sweep
  counted the red as expected.

## 15. test_gluon_3axis_drive_converges (INV-13, INV-26): the ring-down

TARGET: booked 'hold through an impact' (fixture) + 'a real change in
post-impact ring-down amplitude under the single law' (mechanism), two
readings for the owner. The header of the test itself says the scene
is not the conversion's to change; the hold window stays as it is.

15a BASELINE on this binary: free flight exact (|q_err| 0.0021 by f20,
|w_rel| 0, pair spin 0), turtle strike f61 at 9.68 m/s; post-impact
max hold |q_err| 0.1412 (budget 0.0785), band 0.1392, final 0.0043,
nail 0.3000 -> 0.2721 m. The booking of 2026-09-01 read 0.0666 for the
same band: it has doubled since, base unknown.

15b BISECT BY LEVERS, one binary, seven runs:
  DEFAULT, SLEEPER_JUDGE=0, SEAM_MERGE_PRECONDITION=0,
  SEAM_OBB_FACES_FIRST=0, CREATION_DOOR=0, LOGOSPHERE_QUAT_TRUTH=0:
      all BIT-IDENTICAL (max hold 0.1412, band 0.1392, f70 line equal).
  SINGLE_LAW_GATE=0: max hold 0.0610, band 0.0589, final 0.0067,
      nail final 0.1757 m (worse: 42 % compression against 0.2721).
  Nothing of the night's or the stack's touches it. The single-law
  position gate (v4.cpp:5464, dead zone SLOP*BETA instead of SLOP,
  'the gate says what it meant') is the whole difference: it halves
  the angular ring-down when off and lets the nail crush when off.
  Reading: the position pass repairs bond rows too (the nail's length
  is held by it under a 9.7 m/s strike), and something in that pass
  feeds the drive's ring-down. INV-13's own sentence: 'the controller
  never fights a second controller'. Next: read the pass's angular
  half; is a drive row corrected twice (velocity bias + position)?

Census on the way (bookings corrected in TEST_AUDIT, no code):
  test_ramp_race: both spin asserts PASS; the D2 reading retired. Its
    two reds are G-46's and compare travel distances that saturate at
    the ramp's bottom (both racers at rest there, 0.003 m); peak
    speeds say the sphere is the faster (6.722 vs 6.639 m/s). Fixture
    question for the owner.
  test_cube_drop_ladder: R0-R4 green in default; the reds are R5/R6,
    a spinning cube never WALKS (spin buys no translation): G-46's
    family. Booked.

15c READING OF THE MECHANISM (code, no run).
  The 'second controller' hypothesis is REFUTED by the code: the drive's
  rows have a finite torque budget (k 200, c 12: tb = (k|e| + c|w|) dt),
  so they are spring rows; spring rows keep their bias in the velocity
  solve (v4.cpp:4087, 'CONSTRAINTS split, SPRINGS don't') and the
  angular position pass skips them outright (v4.cpp:5364, 'repairing
  them here too would double-apply the force'). The single-law gate
  (5464) lives in the LINEAR pass only. So the lever reaches the drive
  through the nail's distance row and the turtle rows: with the 1 mm
  dead zone the nail is held (0.2721 m final) and the crash energy
  stays in the pair's rotation (max 0.1412); with the 3.5 mm dead zone
  the nail creeps under the pair's weight to 0.1757 m (42 % crush) and
  the rotation rings half as much. The gate on is right; the ring-down
  is the price of a held nail after a 9.7 m/s strike. What the 120
  frames do not say: at f110 the pair still rocks (|w_rel| 0.95 rad/s,
  vzA -0.048 at f100), fifty frames after impact. Settling or limit
  cycle? The booking's missing-law candidate needs that number.
CHANGE 15 (instrument, test-only): DRIVE_FRAMES=N runs the scene longer;
  default 120 unchanged, asserts unchanged. Run at 480.

RUN 15 (DRIVE_FRAMES=480; default 120 rerun bit-identical: 0.1412 /
0.1392). NOT A RING-DOWN. From f190 to f470 the pair's |w_rel| sits
at 0.86 -> 1.12 -> 0.95 rad/s, never below 0.23 after f120, with
|w_A + w_B| EQUAL to |w_rel| to four decimals every printed frame
(f190..f470). The drive error holds 0.020-0.024 rad; the nail creeps
0.2746 -> 0.1883 m (f190 -> f470); zA 0.053 -> 0.086; vzA 0.000.
Final at f479: |q_err| 0.0200, |w_rel| 0.9156 rad/s, nail 0.1817 m.

RCA 15 (canary at f300, both bodies; AUTHORITY_DEBUG on both).
  The scene after the strike: A on the turtle (z 0.054, two turtle
  rows), B on top of it (z 0.312, no contact), held up at the drive's
  45 deg: a CANTILEVER, B's weight on the joint - the load INV-13
  names, arrived at by crash.
  1. The quaternion drive is ONE row per joint (v4.cpp:2921-3076),
     along the current Rodrigues error axis, budget [-inf, inf] here
     because the test's gluon is not force_bounded(); NO row at all
     when e_mag < 1e-6. The comment above it (2774-2786) describes
     three world-axis rows; the code builds one along e.
  2. The nail's three anchor rows carry lever arms. AUTHORITY on B at
     f300: angular-drive -4.42 rad/s per solve, linear-anchor +2.73,
     net -1.69 (on A: +0.17 / -1.01). Two rows correcting each other,
     the pivot-law comment's own words (v4.cpp:4113).
  3. Along the row's axis the solve CONVERGES: w_rel -1.110 (I0) ->
     -0.018 (I24), 25 iterations. But at the substep's end
     omega_A = (-0.51, +0.54, -1.05), omega_B = (-0.63, -0.44, -0.36):
     the relative spin (0.12, 0.98, -0.70), 1.2 rad/s, is PERPENDICULAR
     to the row's axis (0.99, -0.14, -0.01) - its projection is -0.011.
     Nothing in the solve touches it: the drive's damping c acts along
     the error axis only.
  4. That perpendicular spin integrates 1.1/60 = 0.018 rad per frame
     - the standing error 0.020-0.024 is that number - and the angular
     position pass repairs the angle each frame (pseudo-omega
     discarded) while the spin itself is never taken out. A limit
     cycle held at velocity level and masked at position level; the
     'correction that fires forever' of INV-24, measured. The energy
     is B's cantilevered weight through the anchor rows
     (force-at-anchor makes torque).
  VERDICT 15: the booking's 'missing law: settling band' is answered
  by a mechanism gap, not a missing number: a driven joint's damping
  must act on the whole relative angular velocity, not on its
  projection onto the error axis. The comment already says so (three
  world-axis rows). One change for 16, behind a lever: the drive
  builds its three rows. Instrument kept: DRIVE_FRAMES, and the
  per-frame line now prints both omegas.

RUN 15e (omegas printed, 480 frames): the signature resolved. At every
printed frame end from f200 to f470, wA = (0.00, 0.00, 0.00) exactly
(A on the turtle) and wB = (+0.88..+1.09, -0.25..+0.15, -0.25) - B
spins about +X at about 1 rad/s, steadily, while its orientation is
held by repair (error 0.020-0.024). Committed 02541d1.

## 16. The drive's three rows (INV-13, INV-24), behind DRIVE_ROWS=3

HYPOTHESIS: the standing spin survives because the quaternion drive is
one row along e; a spin perpendicular to e meets no row. Three
world-axis rows (bias ANGULAR_BETA * e_k / dt each, clamped at
MAX_ANGULAR_BIAS_VELOCITY per axis, inertia about each axis, budget
(k |e_k| + c |w_k|) dt when force-bounded) damp and correct the whole
relative angular velocity. The split law is untouched: unbounded rows
send their bias to the position pass and damp in velocity; springs
keep theirs in velocity.
CHANGE 16 (engine, lever DRIVE_ROWS=3, default 1 byte-identical):
v4.cpp, the quat branch, before the one-row block. Expected under the
lever: the default 120 unchanged bit for bit; at 480 frames wB -> 0,
the nail stops creeping, the pair sleeps; the one-axis drive test (13)
still converges. If the pair settles, 16 is the mechanism and the
booking's 'settling band' question closes on it; then a sweep alone
with the lever OFF (no ships) and a paired Eden A/B before anything
is called shippable - every live humanoid drive goes through this
branch.

RUN 16 (provenance: 'lever patched', default rerun first).
  Default (DRIVE_ROWS unset), 120: max hold 0.1412, band 0.1392 -
  BIT-IDENTICAL to 15. The lever off is the old binary.
  DRIVE_ROWS=3, 120: free flight identical (0.0021 at f20, spins 0);
  strike at f61 as before; f70 |q_err| 0.0069, f80 0.0110, then
  0.0037 flat from f100 with wA = wB = 0 and vzA 0: ASLEEP. Post
  impact the pair yaws as one body (wA = wB = (0,0,+0.38..+0.60) at
  f70-f80, |w_rel| 0.016) and the turtle's friction stops it by f100.
  Max hold 0.0110 (budget 0.0785), BAND 0.0089, final 0.0037,
  |w_rel| last frame 0.0000, nail 0.2893 m (no creep). Every assert
  PASS: convergence, HOLD, nail hygiene, no pair spin in flight, G-21
  coherence. The test is GREEN under the lever.
  DRIVE_ROWS=3, 480: f120..f470 every number constant (0.0037, 0.2893,
  all spins 0). Settled, sleeping, for six seconds.
  DRIVE_ROWS=3, the one-axis drive test (13): 0.7830 / 0.0024, PASS,
  identical to its default.

VERDICT 16: MECHANISM CONFIRMED BY INTERVENTION. The standing spin, the
  creeping nail and the 'settling band' question were one defect: a
  driven joint's damping acting on the projection of its relative
  angular velocity onto the error axis. Three world-axis rows, which
  the code's own comment described, close it: the pair sleeps. Less
  is more: no new law, no new constant, no damping patch; the row
  count matches the DOF count. Kept behind DRIVE_ROWS=3, default off,
  because every live humanoid drive goes through this branch: the
  jury is the sweep under the lever (alone) and a paired Eden A/B on
  one binary; the flip of the default is the owner's ruling.

## STANDING SUMMARY FOR THE MORNING (after 16; the sweep under the
## lever runs alone as this is written)

Shipped on night/2026-09-04 (defaults byte-identical where it says so):
  1-4, 6-8, 11 (seams, telemetry, strip merge); 13 (drive test in the
  ruled world); 14 (quat/euler sync test states the flip); 15
  (DRIVE_FRAMES instrument, omegas printed, three bookings corrected).
Parked behind levers: 5a SEAM_SPECULATIVE_FACE, 9/12 SEAM_MERGE_ORIENTED,
  11 SEAM_FAMILY_AWAKE, 16 DRIVE_ROWS=3 (default 1).

THE NIGHT'S TWO LESSONS.
  A. Three reds of the second half (13, 14, and the 3-axis booking's
     'ring-down') were laws written before a ruling and never re-read
     after it: G-39's apply gate, the flip, the hold-through-an-impact
     fixture. The sweep counted each as an expected red for a month.
     A ruling that changes a law should visit the tests that state
     the old law the day it lands.
  B. The drive's standing error under load was never stiffness or
     damping: a driven joint built ONE row along its error axis, so the
     relative spin perpendicular to it was damped by nothing, integrated
     each frame, and repaired back by the position pass - a limit cycle
     at velocity level, invisible at position level. Three rows (rows =
     DOFs) close it and the pair sleeps. No new constant. Reference
     for the morning: feat/joint-block-solver assembles 3 anchor rows +
     1 drive row per joint into one block; with three drive rows the
     block is 6x6. The two are complementary (simultaneity vs
     completeness), and 16 alone already sleeps the cantilever.

OWNER RULINGS OWED (new tonight):
  - DRIVE_ROWS=3 as the default (INV-13's mechanism), on the sweep under
    the lever and a paired Eden A/B - both run tonight, results below.
  - The 3-axis test's hold window (hold through a 9.7 m/s strike): keep
    as a crash test, or move the hold into free flight and assert the
    settled cantilever separately (16 makes the second one green).
  - test_ramp_race's G-46 asserts compare travel that saturates at the
    ramp's bottom; the race is in the peak speeds (sphere 6.722 vs cube
    6.639 m/s). Fixture question.
  - The five hand-negated Z quaternions in the engine (journal 13): one
    named bridge, zero behaviour change. Simplification, not a fix.
  - INV-12's wording (count vs force), from iteration 3, still owed.

SWEEP under DRIVE_ROWS=3, alone (360 binaries):
  SWEEP_VERDICT: MOLES 45 (new-red 1, gone-green 1, unaudited 43)
  gone-green: test_gluon_3axis_drive_converges (16's own).
  new-red:    test_physics_drive_two_joints (INV-13: Eva's neck on the
              scalar path and her right shoulder on the quat path
              driven at once, 30 deg flex about X with 20 deg abduct
              about Y, settle within 5 % after 3 s, hold 1 s).
  The lever's cost on a live humanoid joint. RCA below.

RUN 16b: test_physics_drive_two_joints, one binary, lever off / on.
  DRIVE_ROWS=1: neck err 0.0009 (f20) -> 0.0000; shoulder err 0.6319
    -> 0.4402 (f20) -> 0.2094 (f40) -> 0.1531 (f60) -> 0.0297 (f80)
    -> 0.018-0.026 standing (the booked loaded-shoulder error).
  DRIVE_ROWS=3: neck 0.0017 -> 0.0023 (slightly worse, a cross-talk);
    shoulder 0.6319 -> 0.6057 -> 0.6072 -> 0.6255 -> 0.6737 -> 0.6963:
    NEVER STARTS. Not a slow divergence, a joint that does not move.
  So the three rows work on a free pair (STONE cubes, unbounded
  budget) and fail on a loaded, force-bounded humanoid joint. RCA by
  canary on the shoulder's rows.

16c. Reading the shoulder (instruments; two test prints added).
  The joint: 'right_shoulder' k_ang 2000, c_ang 60, force_bounded=0
  (a nail-class gluon, not an OrganicGluon): so its drive rows are
  CONSTRAINT rows, budget [-inf, inf], bias capped at
  MAX_ANGULAR_BIAS_VELOCITY = 4 rad/s, bias sent to the position pass
  unless the body is contact-coupled and the row saturated.
  Canary provenance: the arm's index moves. The solver ran 420 times;
  a canary on P3701 (the test's id at setup) printed 24 frames and
  then followed a swapped-out particle. The test's own ids follow the
  strata's swap-pops through its callback; a canary pid does not. The
  test now prints [ids-final] after the run; the canary uses that id
  and the drive's start frame found by scanning e_mag.
  Suspect before reading: my per-axis clamp of the bias distorts the
  commanded direction (each axis capped at 4 rad/s independently: e =
  (0.52, 0.35, 0) commands (4, 4, 0)/dt-wise instead of e/|e|), where
  the one-row cap preserves it. The cure would be a cap on the vector's
  magnitude, distributed per axis - a fix of the lever's own
  arithmetic, not a new mechanism. To be read, not assumed.

16d. THE READING (canary P914 at F101, the drive 40 solver frames in;
     ANGSOLVE line extended with Ia, Ib, applied inverse inertia, modes,
     both omegas after the write).
  The bridge P910 is KINEMATIC (mode 1), the arm P914 DYNAMIC; only the
  arm receives impulses (Ib 0.0084 about X and Y, 0.00065 about Z).
  DRIVE_ROWS=3, iteration 0: the X row writes wb = (4, 0, 0); the Y row
  wb = (4, 4, 0); the Z row wb = (4, 4, -4). The rows LAND. Iteration
  1: the X row measures w_rel -4e-9 and writes (4, 0, 0) again -
  between iterations the arm's spin went back to zero. Thirty-two
  identical applies, acc -1.078 on X, the arm never moves.
  DRIVE_ROWS=1: the one row writes wb = -4 * axis = (2.49, 3.12,
  -0.43); iteration 1 measures +0.66 (not zero), iteration 2 +1.25, and
  by iteration 31 +3.95: the drive wins against the same contacts.
  WHY: the error at F101 is e = 0.437 * (-0.674, -0.731, +0.105); the
  raw per-axis biases are (-28, -30.6, +4.4) rad/s; capped PER AXIS
  they become (-4, -4, +4): the Z component, 0.046 rad of twist, gets
  the weight of the 0.3 rad flex and abduct, and the commanded motion
  is a third twist about world Z - the arm swung into the torso. Its
  four grazing bone contacts (contact block, r x J) refuse exactly that
  and zero the spin every iteration; the row measures zero and repeats.
  The one-row path caps the MAGNITUDE and keeps the direction. My
  lever's arithmetic was the defect, not the three rows.
CHANGE 16d (lever arithmetic + one flag): the bias vector is capped by
  magnitude, scale = MAX / (beta |e| / dt) when over, applied to every
  component; the three rows share `angular_bias_saturated` (new field
  on Constraint, never set on the default path) so the split law's
  'saturated + contact-coupled -> motion through momentum' reads the
  vector's saturation as the one-row path reads |bias|. Both
  predicates (velocity split, position pass) OR the flag in. Default
  path byte-identical by construction (flag false, predicates
  unchanged). Measured next: two-joints both ways, 3-axis default
  (must read 0.1412 / 0.1392) and lever, one-axis lever.

RUN 16d: the magnitude cap changed the numbers, not the outcome.
  two-joints default: PASS (neck 0.0000, shoulder hold max 0.0363);
  DRIVE_ROWS=3: shoulder 0.6319 -> 0.6104 -> 0.6615 -> 0.6547, FAIL.
  3-axis default 0.1412 / 0.1392 (byte-identical), lever 0.0110 /
  0.0089 PASS; one-axis lever PASS. The direction distortion was real
  and is fixed; it was not what locks the arm.

16e. THE READING, second canary at F101 with the capped rows.
  The shoulder's rows now write wb = (3.24, 2.27, -0.57) = -4 * axis,
  the one-row's own vector. Iteration 1 measures zero again. The rows
  that follow in the same iteration: P914<->P915, the ELBOW, upper arm
  (DYNAMIC) to forearm (KINEMATIC, mode 1), three rows with bias
  (-1e-5, 2e-4, -7e-3): a rest-pose drive at its target. They measure
  w_rel = (3.24, 2.27, -0.57) against the forearm's zero and write
  dL = (-0.0273, -0.0191, +0.00037): the shoulder's impulses, undone
  to the last digit. The forearm is FK-owned; the solver's ledger says
  it does not spin (G-38: a KINEMATIC bone carries a stale ledger).
  Three complete rows make that rest-pose drive a WELD to the FK
  forearm's world orientation. In the one-row world the elbow's single
  row lies along its own error axis (error ~1e-4, an axis of numerical
  noise) and welds one direction; the shoulder wins along the other
  two. That is why the humanoid's drives converge today, and a
  candidate for the shoulder's standing error under load (0.02-0.036):
  a one-axis fight with its own forearm, not the arm's weight.
CHANGE 16e (diagnostic sub-lever, proves the cause by intervention):
  DRIVE_ROWS_DIAG_SKIP_KIN_B=1 builds the one row instead of three for
  a joint whose body_b is KINEMATIC (the elbow here; the shoulder's
  KINEMATIC end is body_a). Not a law - a/b is no parent-child
  convention - a scalpel for one measurement.

RUN 16e (scalpel DRIVE_ROWS_DIAG_SKIP_KIN_B=1 with DRIVE_ROWS=3): the
  shoulder MOVES: 0.6319 -> 0.5337 (f20) -> 0.4951 -> 0.4751 -> 0.4435
  -> 0.3633 (f100) -> 0.2991 -> 0.1771 -> 0.1426 (f160). Hold max
  0.2991, FAIL. Default unchanged (0.0363, PASS). The elbow's weld was
  the lock, by intervention. What the scalpel does not explain: the
  three-row shoulder converges four times slower than the one row
  (0.14 at f160 against 0.03 at f80), with the wrist and hand still
  one-row welds along noise axes. Unmeasured; booked.

16f. THE SCOPE (the rule the measurements point to, replacing the
  scalpel's job; the scalpel stays as the diagnostic it is):
  a joint row damps only what the solver can see. A KINEMATIC endpoint
  is FK-owned and its spin is not in the ledger (G-38's stale twin);
  a complete drive against it welds the DYNAMIC side to a ghost at
  rest. Under DRIVE_ROWS=3 the three rows are built only when BOTH
  endpoints are DYNAMIC; a joint with an FK side keeps the one row it
  always had. Not an edge case on a body: a scope by solver visibility,
  the same kind of scope the block solver takes (force_bounded joints).
  Expected: two-joints under the lever = the default's numbers exactly
  (every joint of Eva's rig has an FK side in that test); the 3-axis
  cantilever (both DYNAMIC) keeps its green; the one-axis test green.
  Then the sweep under the lever, alone, and the paired Eden A/B.

RUN 16f (the scope): two-joints under DRIVE_ROWS=3 reads the default
  to the digit (shoulder 0.0297 at f80, 0.0248 at f160, hold max
  0.0363, PASS); 3-axis lever 0.0110 / 0.0089 PASS; 3-axis default
  0.1412 / 0.1392 (byte-identical, fourth time); one-axis lever PASS.

VERDICT 16 (final): the three-row drive is the mechanism for INV-13's
  hold, scoped to joints the solver fully sees. What it exposed is
  booked as GEDANKEN-75: the humanoid's rest-pose drives against
  FK-owned bones are welds that the one-row drive enforced along one
  noise axis each; a complete drive welds fully. The one-row world's
  humanoids converge BECAUSE their drives are incomplete. The shoulder's
  standing error under load (0.02-0.036) is a candidate victim of that
  one-axis fight, not measured tonight. The write-contract for
  KINEMATIC bones (both ledgers, omega included; PHYSICS_BOARD 351-366)
  is the road that makes the scope unnecessary. Sweep under the lever
  next, alone; then the paired Eden A/B.

SWEEP under DRIVE_ROWS=3 (scoped), alone, after d5b368d:
  SWEEP_VERDICT: MOLES 44 (new-red 0, gone-green 1, unaudited 43)
  gone-green: test_gluon_3axis_drive_converges. Nothing else moved.
  Committed d5b368d (16d-16f, GEDANKEN-75). Eden paired A/B/A/B next
  (one binary, eden target rebuilt, lever off/on/off/on, alone).

EDEN PAIRED A/B/A/B (one binary, eden target rebuilt at d5b368d,
headless --bench 300, alone, steady tail of 179 frames):
  A DRIVE_ROWS=1: 713.0 ms (median 714.5, p90 921)
  B DRIVE_ROWS=3: 873.4 ms (median 870.2, p90 1120)
  A DRIVE_ROWS=1: 702.8 ms (median 708.9, p90 923)
  B DRIVE_ROWS=3: 877.2 ms (median 879.0, p90 1126)
  All-frames: 825 / 914 / 814 / 913 ms.
  THE LEVER COSTS +165..+174 ms PER STEADY FRAME (+24 %), in both
  orders. Reading: on Eden every organic bond between two DYNAMIC
  bodies is a quaternion drive (rung 2: the drive reachable from
  bonds), so the scoped lever triples the angular rows on thousands of
  awake bonds (trees settling, grass). Same door census both ways
  (1739 refused of 11783), physics deterministic per setting.

VERDICT ON THE FLIP (for the owner, not mine): the three-row drive is
  the right law (rows = DOFs; the cantilever sleeps; humanoids
  unchanged under the scope) and it is not free: +24 % of Eden's steady
  frame today. Two roads, both the owner's: (a) flip and pay, (b) the
  perpendicular rows only where a relative spin exists to damp (a row
  with zero bias and zero measured spin applies nothing; skipping it is
  exact for that iteration and an approximation across iterations) -
  an optimization to measure, not a law. Default stays 1 tonight.

STANDING EDEN FINDING (both settings, pre-existing on this branch,
  deterministic): [EXPLOSION WARNING] particle 3945, 17.25 kg, at 45.0
  m/s (ceiling 40) at physics frame 467, pos (45.9, -37.6, 2.8), again
  at f610 (40.3), f773 (40.0); with the lever on: f649 (40.2), f744,
  f804 (41.3), 38-43 bodies suppressed per line. A 17 kg body at 45 m/s
  is a detonation (INV-11). Not the night's doing; whether the door /
  sleeper / seam stack birthed it is not established. Iteration 17
  target: who launches 3945.

## 17. Eden's 45 m/s body (INV-11): who launches P3945

17a. Canary P3945, Eden headless, F455-F468 (default lever): a body in
  FREE FALL with no rows at all - contacts 0, turtle 0, omega 0,
  velocity (0, 0, -35.4) growing to -36.3 by gravity - whose height
  runs 2.7 -> 2.4 -> 5.6 -> 5.3 -> ... -> 2.3, a sawtooth: every dozen
  substeps its POSITION is rewritten 3.3 m up with its velocity kept.
  The detector's 40-45 m/s is that velocity; the body never travels.
  The writer: examples/eden/src/main.cpp:1012-1016, the spirit lights'
  orbit (x, y, z from a Lissajous path, z clamped at 2.0, once per
  render frame; the bench frame is ~700 ms, so several fixed physics
  steps run between writes and the fall shows). The body: created by
  ParticleSystem::queue_light (particle_system.cpp:1002), material
  LIGHT with the comment 'density=0, floats (mass auto-calc to 0)' -
  but the detector reads mass 17.25 kg and the gravity site feeds it
  ('P3945 receives gravity'). Either LIGHT's density is not zero or
  the size set AFTER SetMaterial re-prices the mass. Reading.
  The engine's own rule: a body whose position an external writer
  owns is KINEMATIC; a massless body carries no momentum to fight.
  The judge (G-72) and the door already skip light sources; the
  integrator and the detector do not. Two candidates, one of them the
  code's stated intent (massless light).
17b. LIGHT's density is 0 at every site (materials.h 81/152/226/255)
  and add_particle never prices a light's mass: a queued light is
  massless and cannot receive gravity. P3945 has 17.25 kg and a rotated
  orientation q = (0.8, -0.4, 0.4, 0.2): it is NOT a light. Eden's
  spirit-light loop writes the position of the wrong body: its
  particle_id is the PROMISE queue_light returned
  (queue_particle_addition), and P3944 - the 96 kg rotated trunk that
  detonates the same way - is the neighbouring promise. A stale promise:
  births between the queue and its flush, or the door's refusals (1739
  of 11783 in this scene; the queue's promise cannot know them), shift
  the index and the spirit's orbit lands on a tree. Reading the
  promise's contract next.
17c. Canary P3944 (the 96 kg trunk), F300-F525: the same signature -
  velocity (-0.4, 0.1, -23.6) growing to -35.6 by gravity, height
  sawtoothing 1.8 -> 3.9 -> 3.7 -> ... -> 1.8. A second spirit's
  promise, one index lower, landed on a trunk. Two spirits, two trees.
  The promise (particle_system.cpp:626, live + pending, door-judged so
  refusals never shift it) is broken by any DIRECT birth between the
  queue and its flush (engine.cpp:1479, once per frame): that birth
  takes the index the promise gave away. Instrument next: count direct
  births made while promises are outstanding, report at shutdown like
  INV-38's census; Eden's spirit loop says once when its body is not a
  light. Then the guard (C-121: a broken promise is loud) with its
  test, and Eden's spirits born by create_light - iteration 18.
17d. Provenance: the first instrument patch failed its header anchor
  (the census field's declaration is not the line I assumed), wrote
  nothing, and the Eden run that followed was the baseline again -
  identical explosion lines. Re-patched with a looser anchor, the
  build and the run gated on the patch's success.

RUN 17e (instrument, provenance: 'header/cpp/eden patched' then the
  build): Eden headless, one run.
  [EDEN] spirit light promise P3944 -> a non-light, 96.47 kg,
    0.25x0.25x2.62 m (a trunk); P3945 -> 17.25 kg, 0.13x0.13x1.69 m
    (a branch); P3946, P3947, P3948 -> 0.16-0.28 kg, 0.44x0.31x0.02 m
    (three leaves). ALL FIVE spirits orbit trees.
  [PROMISE BROKEN] 11783 direct births crossed add_particle while
    queued promises were outstanding (first: live 0, pending 1).
  Reading: the sun light is queued first at init (main.cpp:115) and
  stays pending until the first frame's flush (engine.cpp:1479); every
  birth of the world - the door's 11783 - is a direct birth over that
  promise. The spirits, queued at live 3943, were promised 3944-3948;
  the trees born afterwards took those indices. No promise Eden ever
  received was true. The organic floor generator bonds by the same
  promise (organic_floor_generator.cpp 153/286/522) on the loader
  thread: whether its promises are honoured there is a morning
  question with a past (phantom bonds between the wrong tiles would
  look exactly like the seam family). Not claimed; booked.
  Steady frame 705 ms (instrument cost nil).
17f. The first fix (create_light) was refused by its own gate: the
  signature has no radius (create_light(x, y, z, strength, r, g, b)
  against queue_light's (x, y, z, strength, radius, r, g, b)); nothing
  was built. The fix that uses the API as written: Eden flushes the
  queue right after queuing at init (the sun light, then the five
  spirits), before the next direct birth, so each promise is the body
  it names. Two lines in examples/eden/src/main.cpp; the engine's
  contract stays the owner's ruling (GEDANKEN-76). Measured next:
  stranger lines, explosion lines, the promise-break census (what is
  left is the loader thread's), steady frame.

RUN 17g (two flushes; provenance 'eden patched: two flushes', eden
  rebuilt): stranger lines 0, explosion lines 0, NO [PROMISE BROKEN]
  report at all (0 direct births over outstanding promises in the
  whole run, the loader thread's tiles included - the morning question
  answered early: Eden's only broken promises were its own two queues
  at init). Steady frame 614.6 ms (median 590) against 705-721 on the
  same binary within the hour - not a paired A/B (no lever for a
  two-line game fix), read as a hint: five trees no longer rewritten at
  35 m/s every frame stop churning their neighbourhoods.

VERDICT 17: KEEP. The detonations were never physics: an index promised
  by the queue and taken by the world. The instrument stays (always on,
  a census like INV-38's); Eden flushes its queues at init; the
  contract is booked as GEDANKEN-76 for the owner. The regression test
  for the class comes with this commit: queue one body, birth one
  directly, flush, and read what stands at the promised index - RED by
  the engine's contract today, expect-fail with its finding, green the
  day the contract is fixed either way.

RUN 17h: tests/test_queued_promise_kept (standalone, add_engine_at).
  Case A: the queued body's promise was P0; after a direct birth and
  the flush, P0 holds the direct birth (x 5.0), breaks 1: two red, as
  booked. Case B: flushed first, the promise P2 names its body, breaks
  0: green. Committed 5285032 with the instrument, Eden's two flushes,
  GEDANKEN-76 and the audit row (expect fail, born red by contract).
  Sweep alone in flight.

## STANDING SUMMARY FOR THE MORNING (final, after 17)

Shipped on night/2026-09-04, defaults byte-identical unless said:
  1-4, 6-8, 11 (seams: rows-with-impulses telemetry, faces-first,
  the merge judged after it is built, COPLANAR_EPS at SLOP, the strip);
  13, 14 (two tests moved to the ruled world); 15 (DRIVE_FRAMES,
  omegas printed, three bookings corrected); 16d-f (the three-row
  drive behind DRIVE_ROWS=3, scoped to both-DYNAMIC joints); 17 (the
  promise-break census, Eden's flushes, the promise test).
Parked behind levers: SEAM_SPECULATIVE_FACE, SEAM_MERGE_ORIENTED,
  SEAM_FAMILY_AWAKE, DRIVE_ROWS=3, DRIVE_ROWS_DIAG_SKIP_KIN_B.
Registered: GEDANKEN-75 (a joint row damps only what the solver sees),
  GEDANKEN-76 (a queued birth promises an index it cannot keep).

THE NIGHT IN THREE SENTENCES. Three reds were laws written before a
ruling and never re-read after it. The drive's standing error was one
row where three were owed, and completing it exposed that humanoids
converge today because their rest-pose drives against FK bones are
incomplete welds. Eden's detonations were never physics: a promised
index taken by the world, five lights orbiting five trees.

OWNER RULINGS OWED (in the order I would take them):
  1. DRIVE_ROWS=3 as the default: right law, +24 % Eden steady frame
     (paired, both orders). Pay, or damp only where a spin exists.
  2. The queue's contract (G-76): drain-first, handle-at-flush, or a
     loud guard. test_queued_promise_kept turns green with any of them.
  3. The KINEMATIC write-contract (both ledgers, omega included): makes
     the three-row scope moot and the humanoid's one-axis welds honest.
  4. The 3-axis test's hold window (a crash test today; 16 makes a
     free-flight hold + settled cantilever green).
  5. test_ramp_race's G-46 asserts read travel that saturates at the
     ramp's bottom; the race is in the peak speeds.
  6. Five hand-negated Z quaternions -> one named bridge (from_euler).
  7. INV-12's wording (count vs force), from iteration 3.
  8. Unchanged from the day: face rule default (#171, unneeded now),
     F-CROWN, A+B, refusal of unnamed births, Eden game-layer items,
     merge order #166 -> #168 -> #169 -> #170 -> #171 -> #172 -> #173,
     #167 parked; night/2026-09-04 on top of #173.

SWEEP alone after 17 (5285032): SWEEP_VERDICT: MOLES 43 (new-red 0,
  gone-green 0, unaudited 43). Clean; the promise test sits as
  KNOWN-OPEN by its booking.

## 18. Census: five expect-fail rows with no finding
  test_branch_placement_ladder, test_gluon_tree_v34, test_grass_natures,
  test_physics_experiment_01, test_rotation_ladder carry expect: fail
  and an empty known_open. A red without a finding is a mole in
  waiting. One run each, its FAIL lines read and booked.
RUN 18 (one run each, harness, readings booked in TEST_AUDIT):
  branch_placement_ladder: rung 1 red because the branch is never born
    (the door refuses it: centre at the parent's top plane, half inside
    the trunk - F-CROWN's junction), then the test bonds the refused
    id: P1<->P18446744073709551615 (-1) under INV-22's message. The
    wrong refusal for the right reason. Guard candidate: a bond
    endpoint out of range is refused by name (19).
  gluon_tree_v34: legacy law - a 'WEAK 500 N' gluon breaks under 196 N
    static; breaking semantics moved to the force-bounded bond law.
  grass_natures: TRANSLATE ONLY (A+B, the owner's ruling); the rest ok.
  physics_experiment_01: cases 2-3 red; case 3 expects a fall to
    z = -42 m, below the turtle - pre-turtle law; case 2's cause unread.
  rotation_ladder: rungs 1-2 green, 3-4 red with today's numbers (seg
    rot 10.6 deg, joint gap 124 mm; BENT fin 0.04 need > 0.4): the
    joint-block class, untouched by the night.
VERDICT 18: five reds have findings; none is the night's; two are stale
  laws (13/14's class), two are owner fronts, one is the block solver's.

## 19. A bond to nothing is refused by name (from 18's ladder)

TARGET: add_gluon_between (v4.cpp:7071) accepts any pair of ids: a
refused birth's -1 (SIZE_MAX) is bonded silently - get_particle_copy
of a stranger, the row skipped forever by the solver's range guard -
and only a SECOND such bond trips INV-22's 'second live bond' line,
the wrong refusal. Generators bond by PROMISED ids (live + pending),
so the range is the promise range, not the live count.
CHANGE 19 (engine + test): an endpoint at or beyond live + pending is
refused with its own line and not created (like the door: a refusal,
not an abort; counted for tests); a test bonds P0 to SIZE_MAX (refused)
and P0 to a still-pending promise (accepted, live after the flush).
RUN 19 (provenance: 'guard removed from add_particle_with_gluon_to;
  kept in add_gluon_between' - the first entry point already refused a
  stranger on its own and returns -1; my block there was redundant and
  would not compile). tests/test_bond_to_nothing_is_refused: 4 of 4
  green (P0<->P(-1) refused, refusals 0 -> 1, gluons 0 -> 0; a bond to
  the pending promise P1 accepted at promise range 2 / live 1; P1 live
  at z 10.10 after the flush). test_branch_placement_ladder now says
  '[PHYSICS REFUSED] bond P0<->P18446744073709551615: an endpoint lies
  beyond the promise range (live + pending = 1)' - three times, one per
  rung's refused branch - instead of INV-22's second-live-bond line.
VERDICT 19: KEEP (d2777c5). A refusal now names its reason. The
  ladder's own red is unchanged (F-CROWN's junction). Sweep alone in
  flight.
SWEEP alone after 19 (d2777c5): SWEEP_VERDICT: MOLES 43 (new-red 0,
  gone-green 0, unaudited 43). Clean.

## 20. A bond holds a load below its strength? (from 18's census)
TARGET: test_gluon_tree_v34 (legacy): a 'WEAK 500 N' gluon breaks
under B1 alone, 20 kg, 196 N static. If organic bonds tear below their
declared strength, every tree's crown is in it (F-CROWN's 3 torn / 18
taut at frame zero). Baseline: the test's own lines around the break,
then the bond's derived law for the pair.
20a BASELINE (harness): 'UNEXPECTED: Broke with only B1!' - the test's
  break is 'B1's centre fell 0.3 m within 60 frames'; no engine line
  says a bond was removed (removal prints nothing).
RCA 20 (canary P5 = B1, P4 = T3, whole run; physics at 1/30 s, four
  substeps of 1/120):
  - T3 asleep from F43 (vel 0, at_rest 1). B1 born at F61 at z 2.9.
  - The nail's rows exist F61..F72 and vanish at F73: removed by the
    break rule (force >= 0.99 breaking for 12 consecutive frames,
    v4.cpp:5825) - the bond DID break, silently, after exactly 12
    frames. From F77 B1 is in free fall (vz -0.48 -> -1.76) and swings
    about Y (omega_y 6.1 rad/s by F112, q rotated 74 deg).
  - WHY 500 N: the z row's accumulated impulse sits at its cap
    -4.16667 N s on every frame F61..F72; 4.1667 = 500 N x (1/120 s).
    Holding a 20 kg body needs 196 N x (1/120) = 1.63 N s. The row is
    asked for three times the weight and is still short: v_rel 0.061
    m/s unresolved at the last iteration.
  - The nail's angular row is the SCALAR-Z path ('ANGROW-Z': angle
    about Z only, budget inf, limit pi): yaw. Nothing constrains the
    branch's rotation about Y - on plain bodies a NailGluon is a hinge
    about X and Y, and a cantilever pivots down. The full angular row
    (the quaternion path) exists only for quat-driven, PHYSICS-owned
    bodies (G-39's gate): the same ruled-world seam as iteration 13.
  - The z row is priced at eff = 100 for a 20 kg body against a
    sleeping trunk (a stiffness-derived pricing, str 1e8; formula
    read below). The reaction force of a weld carrying a cantilever is
    its weight; the moment is the angular row's. Here the linear row
    carries what the missing angular rows leave.
CHANGE 20 (test-only, same move as 13/14): B1 and the trunk segments
  quat-driven and PHYSICS-owned, so the nail builds its full angular
  row. If the nail then holds 196 N under 500 N, the legacy law stands
  and the fixture was pre-ruled; if the z row still saturates, the
  pricing (eff 100) is the finding.
RUN 20 (ruled world; provenance 'v34 fixture moved to the ruled world'):
  STEP 1 'Result: HOLDING (as expected)'; the z row at F61 reads acc
  -2.04 N s (245 N at 1/120 s: the weight plus the settling), v_rel
  resolved to 1e-4; B1's omega_y 0.16 rad/s and falling. The nail's
  angular row is still the scalar-Z path (a nail has no drive), so
  what holds the cantilever is G-39's anchor torque: the linear rows
  of quat-driven, PHYSICS-owned bodies measure omega x r and apply
  r x J. The eff formula: effective_mass = 1/k of the row's summed
  inverse terms (v4.cpp at the ROWBUILD print).
  The test PASSES - with a hollow second step: 'B2: id=-1', the door
  refuses B2's birth (it overlaps B1 as placed), so 'B1+B2 BROKE' is
  not the staged strike. Booked in the audit row; the staging of B2 is
  a follow-up.
VERDICT 20: KEEP (test-only). The legacy law was right; its fixture
  predated the ruled world (the fourth of the night: 13, 14, the
  3-axis booking, 20). Not swept alone tonight (test-only + audit; the
  morning's sweep covers it).

CORRECTION 20 (read from the door's own line, minutes after the
commit): B1 is 100 kg, not 20 - a 0.5 m WOOD_HARD cube (the material
prices the mass, INV-38's world; the test's '20 kg / 196 N' is a
comment). Its weight is 981 N on a 500 N nail. In the ruled world the
nail carries 245 N because the branch is NOT hanging: it pivots about
the anchor at 0.16 rad/s (END omega_y 0.163, vz -0.04 = 0.16 x 0.25),
dropping 0.08 m in the test's window, under its 0.3 m bar. 'HOLDING'
is a slow pivot; a nail on quat-driven bodies is still a hinge about
X and Y (its only angular row is the scalar-Z path; a NailGluon has
no drive, so the quaternion row is never built) - the anchor torque
of G-39 damps the pivot, nothing stiffens it. Companion to
GEDANKEN-75 (rows = DOFs): a nail that says 'skeleton' should carry
three angular rows or a rigid quaternion row at its rest pose.
Step 2: B2 refused at the door (103.6 mm into the rotated B1).
VERDICT 20, corrected: the test is hollow on both steps and legacy
(status legacy in the audit); it passes by its bars. Kept expect pass
so the sweep does not count a coincidence as a mole, with the
hollowness named; retire or restage is the owner's. The night's
lesson stands in a sharper form: a fixture's comment is not a
measurement - the door printed the mass, the test printed a story.

RULINGS OWED, ADDED AFTER 20:
  9. A NailGluon that says 'skeleton' is a hinge about X and Y on any
     body (its only angular row is the scalar-Z path). Rows = DOFs
     for nails: three angular rows or a rigid quaternion row at the
     rest pose (GEDANKEN-75's companion). Every tree's branch nails
     are in it.
 10. test_gluon_tree_v34: retire, or restage with the door's masses
     and a B2 that does not overlap B1.

FINAL SWEEP alone on the branch head (3e9b704 + this journal):
  SWEEP_VERDICT: MOLES 43 (new-red 0, gone-green 0, unaudited 43).
  The night ends on a clean verdict. Branch night/2026-09-04, on top
  of fix/born-with-material (#173), pushed. Nothing merged by me.

# THE LOOP, RESET (owner, morning of 2026-09-05)

"All this to me sounds like we're edge casing and not working on deeper
understanding and application of physics to our engine via invs and
gedanken, review and reread our principles and get into loop on this
to small incremental improvements."

READ AGAIN: the repo's engine invariants, the physics skill (the board
is the starting point; gedanken first; TDD law-first: DECLARE,
REGISTER, born-red assert, green via Argus, visible on the panel;
levers default off; sweep alone; board updated in the same commit),
the tests skill (one scene, two drivers, full-state narration, the
live panel), INVARIANTS.jsonl in full, the open gedanken, the board's
orientation-truth front and D2, the ledger's tail.

WHAT THE NIGHT DID WRONG, in the skill's own words: fronts not on the
board got worked; the board was never opened and never updated;
G-75/G-76 were written after their code; the three-row drive was
code first; the scope 'three rows only when both endpoints DYNAMIC'
is an if-case; the promise counter and the bond guard are bookkeeping;
four fixtures were moved to the engine's world instead of the engine
being asked which law it breaks.

THE PHYSICS UNDER THE NIGHT'S SYMPTOMS, read one level deeper:
  - A KINEMATIC body is a body of infinite mass with PRESCRIBED motion.
    Its velocity is state. The solver keeps none for it and reads zero,
    so every row against it prices a false relative velocity: a drive's
    rows weld an arm to a forearm read as still (G-75), a nail damps an
    arm against a post that is turning, a contact holds a cube on a
    floor that is sliding away. One missing state, many symptoms.
  - A constraint's rows must span the DOFs it removes (rows = DOFs):
    the one-row drive, the scalar-Z nail. That is INV-16 / D2's
    full-Jacobian row, on the board, with a study complete.

INCREMENT 1 (law first): INV-39 registered, aspirational, born red -
'a body moved from outside carries its motion' (v = dx/dt, omega from
dq/dt for KINEMATIC bodies, before any row is priced; INV-7 keeps it
immovable; INV-20 prices the true relative velocity). Its two
experiments written and PREDICTED before any test or code: G-77 the
moving platform, G-78 the turning post. tests/scenes/scene_writers_motion.h
with test_writers_motion (+ _visual, live panel), Argus-witnessed,
full-state narration, every assert law-tagged. The board carries
SLICE 2 in the same commit. The mechanism is the NEXT increment, behind
KINEMATIC_LEDGER, only after the red is measured.
RUN (increment 1, first measurement, build fresh): 5 of 7 RED.
  G-77: slab x 0 -> 1.254 m; cube x 0.000 all run; slip 1.2542 m (bar
  0.010); seat gap +0.0002..+0.0183; steady cube vx error 0.5000 m/s.
  The cube is held on a floor the ledger calls still.
  G-78: post yaw 0 -> 1.50 rad; arm yaw 0.00, spin 0.000 throughout;
  yaw error 1.5000 rad; separation drift 0.00000 (rigid); coherence
  green for both. The nail transmits none of the turn - not even the
  scalar-Z row's lag: the arm is silent. That silence is the mechanism
  increment's first read (is the row built against a KINEMATIC 'a'?).
