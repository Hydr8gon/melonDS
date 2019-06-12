/*
    Copyright 2016-2019 Arisotura

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

#include "OpenGLSupport.h"

namespace GPU3D
{
namespace GLRenderer
{


DO_PROCLIST(DECLPROC);


bool OpenGL_Init()
{
    DO_PROCLIST(LOADPROC);

    return true;
}

bool OpenGL_BuildShaderProgram(const char* vs, const char* fs, GLuint* ids, const char* name)
{
    int len;
    int res;

    ids[0] = glCreateShader(GL_VERTEX_SHADER);
    len = strlen(vs);
    glShaderSource(ids[0], 1, &vs, &len);
    glCompileShader(ids[0]);

    glGetShaderiv(ids[0], GL_COMPILE_STATUS, &res);
    if (res != GL_TRUE)
    {
        glGetShaderiv(ids[0], GL_INFO_LOG_LENGTH, &res);
        if (res < 1) res = 1024;
        char* log = new char[res+1];
        glGetShaderInfoLog(ids[0], res+1, NULL, log);
        printf("OpenGL: failed to compile vertex shader %s: %s\n", name, log);
        printf("shader source:\n--\n%s\n--\n", vs);
        delete[] log;

        glDeleteShader(ids[0]);

        return false;
    }

    ids[1] = glCreateShader(GL_FRAGMENT_SHADER);
    len = strlen(fs);
    glShaderSource(ids[1], 1, &fs, &len);
    glCompileShader(ids[1]);

    glGetShaderiv(ids[1], GL_COMPILE_STATUS, &res);
    if (res != GL_TRUE)
    {
        glGetShaderiv(ids[1], GL_INFO_LOG_LENGTH, &res);
        if (res < 1) res = 1024;
        char* log = new char[res+1];
        glGetShaderInfoLog(ids[1], res+1, NULL, log);
        printf("OpenGL: failed to compile fragment shader %s: %s\n", name, log);
        //printf("shader source:\n--\n%s\n--\n", fs);
        delete[] log;

        FILE* logf = fopen("shaderfail.log", "w");
        fwrite(fs, len+1, 1, logf);
        fclose(logf);

        glDeleteShader(ids[0]);
        glDeleteShader(ids[1]);

        return false;
    }

    ids[2] = glCreateProgram();
    glAttachShader(ids[2], ids[0]);
    glAttachShader(ids[2], ids[1]);

    return true;
}

bool OpenGL_LinkShaderProgram(GLuint* ids)
{
    int res;

    glLinkProgram(ids[2]);

    glGetProgramiv(ids[2], GL_LINK_STATUS, &res);
    if (res != GL_TRUE)
    {
        glGetProgramiv(ids[2], GL_INFO_LOG_LENGTH, &res);
        if (res < 1) res = 1024;
        char* log = new char[res+1];
        glGetProgramInfoLog(ids[2], res+1, NULL, log);
        printf("OpenGL: failed to link shader program: %s\n", log);
        delete[] log;

        glDeleteShader(ids[0]);
        glDeleteShader(ids[1]);
        glDeleteProgram(ids[2]);

        return false;
    }

    return true;
}

void OpenGL_DeleteShaderProgram(GLuint* ids)
{
    glDeleteShader(ids[0]);
    glDeleteShader(ids[1]);
    glDeleteProgram(ids[2]);
}

void OpenGL_UseShaderProgram(GLuint* ids)
{
    glUseProgram(ids[2]);
}

}
}
