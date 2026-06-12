#include "song.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

bool Song_LoadDatabase(const char* path, SongDB* db)
{
    memset(db, 0, sizeof(SongDB));

    FILE* f = fopen(path, "r");
    if (!f) return false;

    char line[256];
    int currentMode = -1;

    while (fgets(line, sizeof(line), f))
    {
        char* nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        nl = strchr(line, '\r');
        if (nl) *nl = '\0';

        size_t len = strlen(line);
        if (len == 0) continue;

        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == ';' || *p == '#') continue;

        if (p[0] == 'T' && (p[1] == ' ' || p[1] == '\t'))
        {
            const char* v = p + 1;
            while (*v == ' ' || *v == '\t') v++;

            int id;
            float bpm;
            char title[64];
            if (sscanf(v, "%d:%f:%63[^\n]", &id, &bpm, title) >= 2)
            {
                if (db->songCount < MAX_SONGS)
                {
                    SongEntry* e = &db->songs[db->songCount];
                    e->id = id;
                    e->bpm = bpm;
                    if (strlen(title) > 0)
                    {
                        strncpy(e->title, title, sizeof(e->title) - 1);
                    }
                    else
                    {
                        snprintf(e->title, sizeof(e->title), "Song %d", id);
                    }
                    e->hasChart = false;
                    db->songCount++;
                }
            }
        }
        else if (strncmp(p, "MODE", 4) == 0 && (p[4] == ' ' || p[4] == '\t' || p[4] == '\0'))
        {
            const char* modeName = p + 4;
            while (*modeName == ' ' || *modeName == '\t') modeName++;

            if (strcmp(modeName, "END") == 0) break;

            if (db->modeCount < MAX_MODES)
            {
                SongMode* m = &db->modes[db->modeCount];
                strncpy(m->name, modeName, sizeof(m->name) - 1);
                m->songCount = 0;
                currentMode = db->modeCount;
                db->modeCount++;
            }
        }
        else if (p[0] == 'S' && (p[1] == ' ' || p[1] == '\t') && currentMode >= 0)
        {
            const char* v = p + 1;
            while (*v == ' ' || *v == '\t') v++;

            int diff, songId;
            if (sscanf(v, "%d %d", &diff, &songId) == 2)
            {
                SongMode* m = &db->modes[currentMode];
                if (m->songCount < MAX_SONGS_PER_MODE)
                {
                    m->songIds[m->songCount] = songId;
                    m->difficulties[m->songCount] = diff;
                    m->songCount++;
                }
            }
        }
    }

    fclose(f);
    return db->songCount > 0;
}

int Song_FindByID(const SongDB* db, int id)
{
    for (int i = 0; i < db->songCount; i++)
    {
        if (db->songs[i].id == id) return i;
    }
    return -1;
}

int Song_FindMode(const SongDB* db, const char* name)
{
    for (int i = 0; i < db->modeCount; i++)
    {
        if (_stricmp(db->modes[i].name, name) == 0) return i;
    }
    return -1;
}

void Song_PrintDB(const SongDB* db)
{
    printf("SongDB: %d songs, %d modes\n", db->songCount, db->modeCount);
    for (int i = 0; i < db->songCount; i++)
    {
        printf("  %3d: ID=%d BPM=%.1f \"%s\"\n",
            i, db->songs[i].id, db->songs[i].bpm, db->songs[i].title);
    }
    for (int m = 0; m < db->modeCount; m++)
    {
        printf("  MODE %s (%d songs)\n", db->modes[m].name, db->modes[m].songCount);
        for (int s = 0; s < db->modes[m].songCount && s < 5; s++)
        {
            printf("    S %d %d\n", db->modes[m].difficulties[s], db->modes[m].songIds[s]);
        }
        if (db->modes[m].songCount > 5)
            printf("    ... (%d more)\n", db->modes[m].songCount - 5);
    }
}
