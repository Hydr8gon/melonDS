/*
    Copyright 2018 Hydr8gon

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include <algorithm>
#include <chrono>
#include <dirent.h>
#include <malloc.h>
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <switch.h>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <glad/glad.h>

// Deal with conflicting typedefs
#define u64 u64_
#define s64 s64_

#include "../Config.h"
#include "../Savestate.h"
#include "../GPU.h"
#include "../NDS.h"
#include "../SPU.h"
#include "../version.h"

using namespace std;

vector<string> OptionEntries = { "DirectBoot", "Threaded3D", "ScreenRotation", "ScreenLayout", "SwitchOverclock" };
vector<string> OptionDisplay = { "Boot game directly", "Threaded 3D renderer", "Screen rotation", "Screen layout", "Switch overclock" };
vector<unsigned int> OptionValues = { 1, 1, 0, 0, 0 };
vector<vector<string>> OptionValuesDisplay =
{
    { "Off", "On " },
    { "Off", "On " },
    { "0  ", "90 ", "180", "270" },
    { "Natural   ", "Vertical  ", "Horizontal" },
    { "1020 MHz", "1224 MHz", "1581 MHz", "1785 MHz" }
};

u8* BufferData;
AudioOutBuffer* ReleasedBuffer;
AudioOutBuffer AudioBuffer;

u32* Framebuffer;
unsigned int TouchBoundLeft, TouchBoundRight, TouchBoundTop, TouchBoundBottom;

EGLDisplay Display;
EGLContext Context;
EGLSurface Surface;
GLuint Program, VAO, VBO;

Mutex EmuMutex;

const char* const VertexShader =
    "#version 330 core\n"
    "precision mediump float;"

    "layout (location = 0) in vec3 in_pos;"
    "layout (location = 1) in vec2 in_texcoord;"
    "out vec2 vtx_texcoord;"

    "void main()"
    "{"
        "gl_Position = vec4(in_pos, 1.0);"
        "vtx_texcoord = in_texcoord;"
    "}";

const char* const FragmentShader =
    "#version 330 core\n"
    "precision mediump float;"

    "in vec2 vtx_texcoord;"
    "out vec4 fragcolor;"
    "uniform sampler2D texdiffuse;"

    "void main()"
    "{"
        "fragcolor = texture(texdiffuse, vtx_texcoord);"
    "}";

void InitEGL()
{
    EGLConfig config;
    EGLint numconfigs;
    const EGLint attributelist[] = { EGL_NONE };

    Display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(Display, NULL, NULL);
    eglBindAPI(EGL_OPENGL_API);
    eglChooseConfig(Display, attributelist, &config, 1, &numconfigs);
    Surface = eglCreateWindowSurface(Display, config, (char*)"", NULL);
    Context = eglCreateContext(Display, config, EGL_NO_CONTEXT, attributelist);
    eglMakeCurrent(Display, Surface, Surface, Context);
}

void DeinitEGL()
{
    eglMakeCurrent(Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(Display, Context);
    Context = NULL;
    eglDestroySurface(Display, Surface);
    Surface = NULL;
    eglTerminate(Display);
    Display = NULL;
}

void InitRenderer()
{
    float width, height, offset_topX, offset_botX, offset_topY, offset_botY;

    if (OptionValues[3] == 0)
        OptionValues[3] = (OptionValues[2] % 2 == 0) ? 1 : 2;

    if (OptionValues[3] == 1)
    {
        height = 1.0f;
        if (OptionValues[2] % 2 == 0)
            width = height * 0.75;
        else
            width = height * 0.421875;

        offset_topX = offset_botX = -width / 2;
        offset_topY = height;
        offset_botY = 0.0f;
    }
    else
    {
        if (OptionValues[2] % 2 == 0)
        {
            width = 1.0f;
            height = width / 0.75;
        }
        else
        {
            height = 2.0f;
            width = height * 0.421875;
        }

        offset_topX = -width;
        offset_botX = 0.0f;
        offset_topY = offset_botY = height / 2;
    }

    typedef struct
    {
        float position[3];
        float texcoord[2];
    } Vertex;

    Vertex screens[] =
    {
        { { offset_topX + width, offset_topY - height, 0.0f }, { 1.0f, 1.0f } },
        { { offset_topX,         offset_topY - height, 0.0f }, { 0.0f, 1.0f } },
        { { offset_topX,         offset_topY,          0.0f }, { 0.0f, 0.0f } },
        { { offset_topX,         offset_topY,          0.0f }, { 0.0f, 0.0f } },
        { { offset_topX + width, offset_topY,          0.0f }, { 1.0f, 0.0f } },
        { { offset_topX + width, offset_topY - height, 0.0f }, { 1.0f, 1.0f } },

        { { offset_botX + width, offset_botY - height, 0.0f }, { 1.0f, 1.0f } },
        { { offset_botX,         offset_botY - height, 0.0f }, { 0.0f, 1.0f } },
        { { offset_botX,         offset_botY,          0.0f }, { 0.0f, 0.0f } },
        { { offset_botX,         offset_botY,          0.0f }, { 0.0f, 0.0f } },
        { { offset_botX + width, offset_botY,          0.0f }, { 1.0f, 0.0f } },
        { { offset_botX + width, offset_botY - height, 0.0f }, { 1.0f, 1.0f } }
    };

    if (OptionValues[2] == 1 || OptionValues[2] == 2)
    {
        Vertex* copy = (Vertex*)malloc(sizeof(screens));
        memcpy(copy, screens, sizeof(screens));
        memcpy(screens, &copy[6], sizeof(screens) / 2);
        memcpy(&screens[6], copy, sizeof(screens) / 2);
    }

    TouchBoundLeft = (screens[8].position[0] + 1) * 640;
    TouchBoundRight = (screens[6].position[0] + 1) * 640;
    TouchBoundTop = (-screens[8].position[1] + 1) * 360;
    TouchBoundBottom = (-screens[6].position[1] + 1) * 360;

    for (unsigned int i = 0; i < OptionValues[2]; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            for (int k = 0; k < 12; k += 6)
            {
                screens[k].position[j] = screens[k + 1].position[j];
                screens[k + 1].position[j] = screens[k + 2].position[j];
                screens[k + 2].position[j] = screens[k + 4].position[j];
                screens[k + 3].position[j] = screens[k + 4].position[j];
                screens[k + 4].position[j] = screens[k + 5].position[j];
                screens[k + 5].position[j] = screens[k].position[j];
            }
        }
    }

    GLint vsh = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vsh, 1, &VertexShader, NULL);
    glCompileShader(vsh);

    GLint fsh = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fsh, 1, &FragmentShader, NULL);
    glCompileShader(fsh);

    Program = glCreateProgram();
    glAttachShader(Program, vsh);
    glAttachShader(Program, fsh);
    glLinkProgram(Program);

    glDeleteShader(vsh);
    glDeleteShader(fsh);

    glGenVertexArrays(1, &VAO);
    glBindVertexArray(VAO);

    glGenBuffers(1, &VBO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(screens), screens, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texcoord));
    glEnableVertexAttribArray(1);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glUseProgram(Program);
}

void DeinitRenderer()
{
    glDeleteBuffers(1, &VBO);
    glDeleteVertexArrays(1, &VAO);
    glDeleteProgram(Program);
}

void FillAudioBuffer()
{
    s16 buf_in[984 * 2];
    s16* buf_out = (s16*)BufferData;

    int num_in = SPU::ReadOutput(buf_in, 984);
    int num_out = 1440;

    int margin = 6;
    if (num_in < 984 - margin)
    {
        int last = num_in - 1;
        if (last < 0)
            last = 0;

        for (int i = num_in; i < 984 - margin; i++)
            ((u32*)buf_in)[i] = ((u32*)buf_in)[last];

        num_in = 984 - margin;
    }

    float res_incr = (float)num_in / num_out;
    float res_timer = 0;
    int res_pos = 0;

    for (int i = 0; i < 1440; i++)
    {
        buf_out[i * 2] = buf_in[res_pos * 2];
        buf_out[i * 2 + 1] = buf_in[res_pos * 2 + 1];

        res_timer += res_incr;
        while (res_timer >= 1)
        {
            res_timer--;
            res_pos++;
        }
    }
}

void AdvFrame(void* args)
{
    while (true)
    {
        chrono::steady_clock::time_point start = chrono::steady_clock::now();

        mutexLock(&EmuMutex);
        NDS::RunFrame();
        mutexUnlock(&EmuMutex);
        memcpy(Framebuffer, GPU::Framebuffer, 256 * 384 * 4);

        while (chrono::duration_cast<chrono::duration<double>>(chrono::steady_clock::now() - start).count() < (float)1 / 60);
    }
}

void PlayAudio(void* args)
{
    while (true)
    {
        FillAudioBuffer();
        audoutPlayBuffer(&AudioBuffer, &ReleasedBuffer);
    }
}

int main(int argc, char** argv)
{
    consoleInit(NULL);
    pcvInitialize();

    bool options = false;
    string rompath = "sdmc:/";

    fstream config;
    config.open("melonds.ini", ios::in);
    string line;
    while (getline(config, line))
    {
        vector<string>::iterator iter = find(OptionEntries.begin(), OptionEntries.end(), line.substr(0, line.find("=")));
        if (iter != OptionEntries.end())
            OptionValues[iter - OptionEntries.begin()] = stoi(line.substr(line.find("=") + 1));
    }
    config.close();

    while (rompath.find(".nds", (rompath.length() - 4)) == string::npos)
    {
        consoleClear();
        printf("melonDS " MELONDS_VERSION "\n");
        printf(MELONDS_URL);

        unsigned int selection = 0;
        vector<string> files;

        DIR* dir = opendir(rompath.c_str());
        dirent* entry;
        while ((entry = readdir(dir)))
        {
            string name = entry->d_name;
            if (entry->d_type == DT_DIR || name.find(".nds", (name.length() - 4)) != string::npos)
                files.push_back(name);
        }
        closedir(dir);
        sort(files.begin(), files.end());

        while (true)
        {
            hidScanInput();
            u32 pressed = hidKeysDown(CONTROLLER_P1_AUTO);

            if (options)
            {
                if (pressed & KEY_A)
                {
                    OptionValues[selection]++;
                    if (OptionValues[selection] > OptionValuesDisplay[selection].size() - 1)
                        OptionValues[selection] = 0;
                }
                else if (pressed & KEY_UP && selection > 0)
                {
                    selection--;
                }
                else if (pressed & KEY_DOWN && selection < OptionDisplay.size() - 1)
                {
                    selection++;
                }
                else if (pressed & KEY_X)
                {
                    config.open("melonds.ini", ios::out);
                    for (unsigned int i = 0; i < OptionDisplay.size(); i++)
                        config << OptionEntries[i] + "=" + to_string(OptionValues[i]) + "\n";
                    config.close();

                    options = false;
                    break;
                }

                for (unsigned int i = 0; i < OptionDisplay.size(); i++)
                {
                    if (i == selection)
                    {
                        printf(CONSOLE_WHITE"\x1b[%d;1H%s", i + 4, OptionDisplay[i].c_str());
                        printf(CONSOLE_WHITE"\x1b[%d;30H%s", i + 4, OptionValuesDisplay[i][OptionValues[i]].c_str());
                    }
                    else
                    {
                        printf(CONSOLE_RESET"\x1b[%d;1H%s", i + 4, OptionDisplay[i].c_str());
                        printf(CONSOLE_RESET"\x1b[%d;30H%s", i + 4, OptionValuesDisplay[i][OptionValues[i]].c_str());
                    }
                }

                printf(CONSOLE_RESET"\x1b[45;1HPress X to return to the file browser.");
            }
            else
            {
                if (pressed & KEY_A && files.size() > 0)
                {
                    rompath += "/" + files[selection];
                    break;
                }
                else if (pressed & KEY_B && rompath != "sdmc:/")
                {
                    rompath = rompath.substr(0, rompath.rfind("/"));
                    break;
                }
                else if (pressed & KEY_UP && selection > 0)
                {
                    selection--;
                }
                else if (pressed & KEY_DOWN && selection < files.size() - 1)
                {
                    selection++;
                }
                else if (pressed & KEY_X)
                {
                    options = true;
                    break;
                }

                for (unsigned int i = 0; i < files.size(); i++)
                {
                    if (i == selection)
                        printf(CONSOLE_WHITE"\x1b[%d;1H%s", i + 4, files[i].c_str());
                    else
                        printf(CONSOLE_RESET"\x1b[%d;1H%s", i + 4, files[i].c_str());
                }

                printf(CONSOLE_RESET"\x1b[45;1HPress X to open the options menu.");
            }

            consoleUpdate(NULL);
        }
    }

    Config::Load();

    if (!Config::HasConfigFile("bios7.bin") || !Config::HasConfigFile("bios9.bin") || !Config::HasConfigFile("firmware.bin"))
    {
        consoleClear();
        printf("One or more of the following required files don't exist or couldn't be accessed:");
        printf("bios7.bin -- ARM7 BIOS\n");
        printf("bios9.bin -- ARM9 BIOS\n");
        printf("firmware.bin -- firmware image\n\n");
        printf("Dump the files from your DS and place them in sdmc:/switch/melonds");

        while (true)
            consoleUpdate(NULL);
    }

    NDS::Init();

    string srampath = rompath.substr(0, rompath.rfind(".")) + ".sav";
    string statepath = rompath.substr(0, rompath.rfind(".")) + ".mln";

    if (!NDS::LoadROM(rompath.c_str(), srampath.c_str(), Config::DirectBoot))
    {
        consoleClear();
        printf("Failed to load ROM. Make sure the file can be accessed.");

        while (true)
            consoleUpdate(NULL);
    }

    if (OptionValues[4] == 0)
        pcvSetClockRate(PcvModule_Cpu, 1020000000);
    else if (OptionValues[4] == 1)
        pcvSetClockRate(PcvModule_Cpu, 1224000000);
    else if (OptionValues[4] == 2)
        pcvSetClockRate(PcvModule_Cpu, 1581000000);
    else
        pcvSetClockRate(PcvModule_Cpu, 1785000000);

    consoleClear();
    fclose(stdout);
    InitEGL();
    gladLoadGL();
    InitRenderer();

    Thread main;
    threadCreate(&main, AdvFrame, NULL, 0x80000, 0x30, 1);
    threadStart(&main);

    audoutInitialize();
    audoutStartAudioOut();

    BufferData = (u8*)memalign(0x1000, (1440 * 2 * 2 + 0xfff) & ~0xfff);
    AudioBuffer.next = NULL;
    AudioBuffer.buffer = BufferData;
    AudioBuffer.buffer_size = (1440 * 2 * 2 + 0xfff) & ~0xfff;
    AudioBuffer.data_size = 1440 * 2 * 2;
    AudioBuffer.data_offset = 0;

    Thread audio;
    threadCreate(&audio, PlayAudio, NULL, 0x80000, 0x30, 0);
    threadStart(&audio);

    Framebuffer = (u32*)malloc(256 * 384 * 4);

    HidControllerKeys keys[] = { KEY_A, KEY_B, KEY_MINUS, KEY_PLUS, KEY_RIGHT, KEY_LEFT, KEY_UP, KEY_DOWN, KEY_ZR, KEY_ZL, KEY_X, KEY_Y };

    while (true)
    {
        hidScanInput();
        u32 pressed = hidKeysDown(CONTROLLER_P1_AUTO);
        u32 released = hidKeysUp(CONTROLLER_P1_AUTO);

        if (pressed & KEY_L || pressed & KEY_R)
        {
            Savestate* state = new Savestate(const_cast<char*>(statepath.c_str()), pressed & KEY_L);
            mutexLock(&EmuMutex);
            NDS::DoSavestate(state);
            mutexUnlock(&EmuMutex);
            delete state;
        }

        for (int i = 0; i < 12; i++)
        {
            if (pressed & keys[i])
                NDS::PressKey(i > 9 ? i + 6 : i);
            else if (released & keys[i])
                NDS::ReleaseKey(i > 9 ? i + 6 : i);
        }

        if (hidTouchCount() > 0)
        {
            touchPosition touch;
            hidTouchRead(&touch, 0);

            if (touch.px > TouchBoundLeft && touch.px < TouchBoundRight && touch.py > TouchBoundTop && touch.py < TouchBoundBottom)
            {
                int x, y;
                if (OptionValues[2] == 0)
                {
                    x = (touch.px - TouchBoundLeft) * 256.0f / (TouchBoundRight - TouchBoundLeft);
                    y = (touch.py - TouchBoundTop) * 256.0f / (TouchBoundRight - TouchBoundLeft);
                }
                else if (OptionValues[2] == 1)
                {
                    x = (touch.py - TouchBoundTop) * 192.0f / (TouchBoundRight - TouchBoundLeft);
                    y = 192 - (touch.px - TouchBoundLeft) * 192.0f / (TouchBoundRight - TouchBoundLeft);
                }
                else if (OptionValues[2] == 2)
                {
                    x = (touch.px - TouchBoundLeft) * -256.0f / (TouchBoundRight - TouchBoundLeft);
                    y = 192 - (touch.py - TouchBoundTop) * 256.0f / (TouchBoundRight - TouchBoundLeft);
                }
                else
                {
                    x = (touch.py - TouchBoundTop) * -192.0f / (TouchBoundRight - TouchBoundLeft);
                    y = (touch.px - TouchBoundLeft) * 192.0f / (TouchBoundRight - TouchBoundLeft);
                }
                NDS::PressKey(16 + 6);
                NDS::TouchScreen(x, y);
            }
        }
        else
        {
            NDS::ReleaseKey(16 + 6);
            NDS::ReleaseScreen();
        }

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_BGRA, GL_UNSIGNED_BYTE, Framebuffer);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_BGRA, GL_UNSIGNED_BYTE, &Framebuffer[256 * 192]);
        glDrawArrays(GL_TRIANGLES, 6, 12);
        eglSwapBuffers(Display, Surface);
    }

    NDS::DeInit();
    audoutExit();
    DeinitRenderer();
    DeinitEGL();
    pcvExit();
    consoleExit(NULL);
    return 0;
}
