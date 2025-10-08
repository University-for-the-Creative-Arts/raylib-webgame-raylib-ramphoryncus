//main.cpp
#include "raylib.h"
#include <cmath>
#include <vector>
#include <string>
#include <cstdio>
#include <array> // for clock direction stat breakdown


// Helper function for distance between two Vector2 points if not provided by Raylib
static float Vector2Distance(Vector2 v1, Vector2 v2) {
    float dx = v2.x - v1.x;
    float dy = v2.y - v1.y;
    return sqrtf(dx*dx + dy*dy);
}
// For web builds
#ifdef __EMSCRIPTEN__
    #include <emscripten/emscripten.h>
    #include <emscripten/fetch.h> //Requires -sFETCH
#else
    #include <fstream>  // for desktop CSV fallback std::ofstream
#endif
// Trial data structure
struct Trial {
    int targetIndex;       //which of the 12 clock positions (0...11)
    float spawnTime;       //Seconds
    float clickTime;       //Seconds
    float reactionMs;      //Computed
    bool hitOuter;         //True if hit anywhere in outer ring
    bool hitInner;         //True if hit in bullseye
    int score;             // 0 / 5 / 10
};

static const int SCREEN_W = 1280;
static const int SCREEN_H = 720;
static const Vector2 CENTER = { SCREEN_W * 0.5f, SCREEN_H * 0.5f };

static const float CLOCK_RADIUS = 260.0f; //distance from centre to target
static const float R_OUT = 36.0f;         //outer ring radius (5 pts)
static const float R_IN = 14.0f;          //inner bullseye radius (10 pts)
static const float CENTER_READY_RADIUS = 28.0f; //must return here and click

static const int NUM_TRIALS = 100; //total trials per game
// enum for game phase
enum class Phase {
    WaitAtCenter, //waiting for player to return to centre and click
    TargetLive,   //target spawned; waiting for hit
    Finished      //all trials done; show stats & (on web) POST
};

struct GameState {
    Phase phase = Phase::WaitAtCenter;
    int trialsDone = 0;
    int currentTargetIndex = 0; //which of the 12 directions
    Vector2 currentTargetPos{};
    float targetSpawnTime = 0.0f;
    std::vector<Trial> trials;
    bool posted = false;  //prevents double POST
};
// Global game state
static GameState G;
// Return position on clock face for index 0..11
static Vector2 TargetPosForIndex(int idx) {
    // 12 positions like a clock: 12 o'clock, then clockwise every 30 degrees
    //Convert so 0 points up; angle = -90 + idx*30 degrees
    float angleDeg = -90.0f + idx * 30.0f;
    float a = angleDeg * DEG2RAD;
    return { CENTER.x + CLOCK_RADIUS * cosf(a), CENTER.y + CLOCK_RADIUS * sinf(a)};
}
// Return random int 0..11
static int RandomClockIndex() { return GetRandomValue(0,11);}
// POST results to server (web only)
#ifdef __EMSCRIPTEN__
static void PostJSON(const std::string& json)
{
    // NOTE: I must replace this with my OWN endpoint with CORS enabled (HTTPS)
    const char* POST_ENDPOINT = "https://example.com/my-endpoint";

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "POST");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.requestData = json.c_str();
    attr.requestDataSize = json.size();

    //Set headers
    const char* headers[] = { "Content-Type", "application/json", 0 };
    attr.requestHeaders = headers;
    attr.onsuccess = [](emscripten_fetch_t* fetch) {
        //Optionally inspect fetch->data / fetch->numBytes
        emscripten_fetch_close(fetch); // Free data associated with the fetch
    };
    attr.onerror =[](emscripten_fetch_t* fetch) {
        //Error!
        emscripten_fetch_close(fetch); // Free data associated with the fetch
    };

    emscripten_fetch(&attr, POST_ENDPOINT);
}
#endif
// Start a new trial by selecting random target & recording spawn time
static void BeginTrial()
{
    G.currentTargetIndex = RandomClockIndex();
    G.currentTargetPos = TargetPosForIndex(G.currentTargetIndex);
    G.targetSpawnTime = GetTime();
    G.phase = Phase::TargetLive;
}
// Record a hit or miss; advance state
static void RecordHit(const Vector2 mouse, float timeNow)
{
    float dist = Vector2Distance(mouse, G.currentTargetPos);
    bool hitInner = dist <= R_IN;
    bool hitOuter = dist <= R_OUT;
    int score = hitInner ? 10 : (hitOuter ? 5 : 0);

    if (!hitOuter) return; //only progress when target is hit

    Trial t{};
    t.targetIndex = G.currentTargetIndex;
    t.spawnTime = G.targetSpawnTime;
    t.clickTime = timeNow;
    t.reactionMs = (t.clickTime - t.spawnTime) * 1000.0f;
    t.hitOuter = hitOuter;
    t.hitInner = hitInner;
    t.score = score;

    G.trials.push_back(t);
    G.trialsDone++;

    if (G.trialsDone >= NUM_TRIALS) {
        G.phase = Phase::Finished;
    }
    else {
        G.phase = Phase::WaitAtCenter;
    }
}
// summary results for JSON POST
static std::string BuildResultJSON()
{
    int totalScore = 0, bullseyes = 0, hits = 0;
    double sumRt = 0.0;
    for (const auto& t : G.trials) {
        totalScore += t.score;
        if (t.hitOuter) hits++;
        if (t.hitInner) bullseyes++;
        sumRt += t.reactionMs;
    }
    double avgRt = G.trials.empty() ? 0.0 : (sumRt / G.trials.size());
    double hitRate = (double)hits / (double)NUM_TRIALS;

    //Simple compact JSON (no external libs)
    char buf[4096];
    std::snprintf(buf, sizeof(buf),
        "{"
        "\"trials\":%d,"
        "\"hits\":%d,"
        "\"bullseyes\":%d,"
        "\"totalScore\":%d,"
        "\"avgReactionMs\":%.3f,"
        "\"hitRate\":%.3f,"
        "}",
        NUM_TRIALS, hits, bullseyes, totalScore, avgRt, hitRate
    );
    return std::string(buf);
}
// Per-direction stats
static const char* CLOCK_LABELS[12] = { 
    "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"
};
// Aggregated stats per direction
struct DirAgg {
    int n = 0;          //trials at this direction
    int bulls = 0;      //inner-ring hits
    double sumMs = 0.0; //sum of RT
    double minMs = 1e9;
    double maxMs = 0.0;
};
// Compute per-direction stats
static std::array<DirAgg,12> ComputeDirStats(){
    std::array<DirAgg,12> A{};
    for (const auto& t : G.trials) {
        int i = t.targetIndex;
        auto& d = A[i];
        d.n++;
        d.bulls += t.hitInner ? 1 :0;
        d.sumMs += t.reactionMs;
        if (t.reactionMs < d.minMs) d.minMs = t.reactionMs;
        if (t.reactionMs > d.maxMs) d.maxMs = t.reactionMs;
    }
    //if no samples, make min 0 for nicer printing
    for (auto& d : A) if (d.n ==0) d.minMs = 0.0;
    return A;
}
// Draw per-direction stats table
static void DrawStatsTable(const std::array<DirAgg,12>& A, int x, int y) {
    const int w = 300;
    const int rowH = 22;

    DrawRectangle(x-10, y-16, w+20, rowH*15, Color{0,0,0,120});
    DrawText("Per-Direction stats", x, y-12, 22, RAYWHITE);
    y += 14;

    DrawText("Dir     N   Avg(ms)   Min   Max   Bull%", x, y, 18, GRAY);
    y += rowH;
// for loop to print each direction's stats
    for (int i = 0; i < 12; ++i) {
        const auto& d = A[i];
        double avg = d.n ? (d.sumMs / d.n) : 0.0;
        double bullPct = d.n ? (100.0 * d.bulls / d.n) : 0.0;

        char line[128];
        std::snprintf(line, sizeof(line),
            "%-3s  %3d  %7.1f  %5.1f %5.1f  %5.1f%%",
            CLOCK_LABELS[i], d.n, avg, d.minMs, d.maxMs, bullPct);
        
        DrawText(line, x, y, 18, RAYWHITE);
        y += rowH;
    }
}
// detailed results for all trials
static std::string BuildCSV() { 
    std::string csv = "trial,targetIndex,spawntime,clickTime,reactionMs,hitOuter,hitInner,score\n";
    for (size_t i = 0; i < G.trials.size(); i++) {
        const auto& t = G.trials[i];
        csv += std::to_string(i+1) + "," +
               std::to_string(t.targetIndex) + "," +
               std::to_string(t.spawnTime) + "," +
               std::to_string(t.clickTime) + "," +
               std::to_string(t.reactionMs) + "," +
               (t.hitOuter ? "1" : "0") + "," +
               (t.hitInner ? "1" : "0") + "," +
               std::to_string(t.score) + "\n";
    }
    return csv;
}

static std::string BuildDirSummaryCSV() {
    auto A = ComputeDirStats();
    std::string csv = "dir,n,avg_ms,min_ms,max_ms,bull_pct\n";
    for (int i =0; i < 12; ++i) {
        const auto& d = A[i];
        double avg = d.n ? (d.sumMs / d.n) : 0.0;
        double bull = d.n ? (100.0 * d.bulls / d.n) : 0.0;
        csv += std::string(CLOCK_LABELS[i]) + "," +
               std::to_string(d.n) + "," +
               std::to_string(avg) + "," +
               std::to_string(d.minMs) + "," +
               std::to_string(d.maxMs) + "," +
               std::to_string(bull) + "\n";
    }
    return csv;
}

#ifdef __EMSCRIPTEN__ // Download text file in browser
#include <emscripten.h>
static void DownloadText(const std::string& filename, const std::string& text) {
    EM_ASM({
        const name = UTF8ToString($0);
        const data = UTF8ToString($1);
        const blob = new Blob([data], {type:'text/csv'});
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = name;
        a.click();
        setTimeout(() => URL.revokeObjectURL(a.href), 1500);
    }, filename.c_str(), text.c_str());
}
#endif
// Main entry point/////////////////
int main()
{
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Clock Face Accuracy Reaction Test (CFART)");
    SetTargetFPS(60);

    //Seed randomness
    SetRandomSeed((unsigned int)GetTime());

    //Start by asking the player to return to centre and click
    G.phase = Phase::WaitAtCenter;

    while (!WindowShouldClose())
    {
        float now = GetTime();
        Vector2 mouse = GetMousePosition();

        //Logic
        if (G.phase == Phase::WaitAtCenter) {
            //Wait until mouse in within centre radius & user clicks to begin next trial
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (Vector2Distance(mouse, CENTER) <= CENTER_READY_RADIUS) {
                    BeginTrial();
                }
            }
        }
        else if (G.phase == Phase::TargetLive) {
            //Wait until user clicks; record hit/miss
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                RecordHit(mouse, now);
            }
        }
        else if (G.phase == Phase::Finished) {
        #ifdef __EMSCRIPTEN__
            if (!G.posted) {
                std::string json = BuildResultJSON();
                PostJSON(json);
                G.posted = true;
            }
        #endif

            //NEW: download CSV
            if (IsKeyPressed(KEY_D)) {
                auto csv = BuildCSV();
        #ifdef __EMSCRIPTEN__
                DownloadText("cfart_results.csv", csv);
        #else
                //Desktop fallback(needs #include <fstream>)
                std::ofstream f("cfart_results.csv", std::ios::binary);
                if (f) f << csv;
        #endif
            }

            //Download Per_Direction stats CSV
            if (IsKeyPressed(KEY_P)) {
                auto csv = BuildDirSummaryCSV();
        #ifdef __EMSCRIPTEN__
                DownloadText("cfart_dir_summary.csv", csv);
        #else
                std::ofstream f("cfart_dir_summary.csv", std::ios::binary);
                if (f) f << csv;
        #endif
            }
            //All done; wait for user to close window
            if (IsKeyPressed(KEY_R)) {
                //Reset
                G = GameState{};
                G.phase = Phase::WaitAtCenter;
            }
        }

        //Draw
        BeginDrawing();
        ClearBackground(Color{18, 18, 24, 255});

        //Centre reticle
        DrawCircleLinesV(CENTER, CENTER_READY_RADIUS, Color{180, 180, 180, 255});
        DrawCircleV(CENTER, 3, RAYWHITE);
        DrawText("Return to CENTRE and CLICK to SPAWN targets", 20, 20, 20, RAYWHITE);

        //Clock helper (faint)
        for (int i = 0; i < 12; ++i) {
            Vector2 p = TargetPosForIndex(i);
            DrawCircleLinesV(p, 10, Color{60,60,60,255});
        }

        //UI Text
        char info[256];
        std::snprintf(info, sizeof(info), "Trial %d / %d  (R to RESTART when finished)",
            (G.trialsDone < NUM_TRIALS) ? (G.trialsDone + (G.phase==Phase::TargetLive ? 1:0)) : NUM_TRIALS,
            NUM_TRIALS);
        DrawText(info, 20, 50, 20, RAYWHITE);

        if (G.phase == Phase::TargetLive) {
            //Draw current target
            DrawCircleV(G.currentTargetPos, R_OUT, Color{40,140,255,200});
            DrawCircleV(G.currentTargetPos, R_IN, Color{245,245,255,255});
            DrawCircleLinesV(G.currentTargetPos, R_OUT, RAYWHITE);
            DrawCircleLinesV(G.currentTargetPos, R_IN, DARKBLUE);

            //Reaction time live readout
            float rt = (now - G.targetSpawnTime) * 1000.0f;
            char rtbuf[64];
            std::snprintf(rtbuf, sizeof(rtbuf), "RT: %.1f ms", rt);
            DrawText(rtbuf, (int)G.currentTargetPos.x + 40, (int)G.currentTargetPos.y - 10, 20, RAYWHITE);
        }

        if (G.phase == Phase::Finished) {
            //Summaries
            int totalScore = 0, bull = 0;
            double sumRt = 0.0;
            for (auto& t : G.trials) {
                totalScore += t.score;
                if (t.hitInner) bull++;
                sumRt += t.reactionMs;
            }
            double avgRt = G.trials.empty() ? 0.0 : sumRt / G.trials.size();
            int hits = (int)G.trials.size();

            DrawRectangle(0, SCREEN_H - 140, SCREEN_W, 140, Color{0,0,0,140});
            DrawText("Finished!!", 20, SCREEN_H - 130, 30, RAYWHITE);
            char s[256];
            std::snprintf(s, sizeof(s),
                "Hits: %d/%d  Bullseyes: %d  Toatal Score: %d  Avg RT: %.1f ms   (Press R to RESTART)",
                hits, NUM_TRIALS, bull, totalScore, avgRt);
            DrawText(s, 20, SCREEN_H - 90, 24, YELLOW);
#ifdef __EMSCRIPTEN__
            DrawText(G.posted ? "Results posted." : "Posting results...", 20, SCREEN_H - 60, 20, LIGHTGRAY);
#else
            DrawText("Web POST disabled in native build.", 20, SCREEN_H - 60, 20, LIGHTGRAY);
#endif
            // Per-Direction stats table (top right)
            auto dirStats = ComputeDirStats();
            DrawStatsTable(dirStats, SCREEN_W - 320, 20);

            //NEW hint for CSV download
            DrawText("Press D to download detailed results CSV", 20, SCREEN_H -30, 20, LIGHTGRAY);
            //NEW hint for per-direction summary CSV
            DrawText("Press P to download per-direction summary CSV", SCREEN_W - 550, SCREEN_H -30, 20, LIGHTGRAY);
        }
        EndDrawing();
    }
    CloseWindow();
    return 0;
}