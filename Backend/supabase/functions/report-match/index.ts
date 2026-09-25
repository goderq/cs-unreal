// CS-Fusion: the v1.2 match report endpoint, retired.
//
// v1.2 builds reported matches here with participants and times chosen by the
// client, and the database counted duplicate players several times (see
// docs/AUDIT.md A3). v2.0 reports through functions/match with server-side
// start times and participation tickets. This stub stays deployed only so
// that old builds get a clear answer instead of a silent failure.

Deno.serve(() =>
    new Response(JSON.stringify({ error: "this version of CS-Fusion is too old to record matches - please update" }), {
        status: 410,
        headers: { "content-type": "application/json" },
    })
);
