#include "3rdparty/pez.h"
#include "3rdparty/gl3w.h"
#define DR_FSW_IMPLEMENTATION
#include "3rdparty/dr_fsw.h"
#define DR_IMPLEMENTATION
#include "3rdparty/dr.h"
#include "3rdparty/nuklear.h"
#include "3rdparty/nuklear_pez_gl3.h"
#include "3rdparty/bass.h"
#pragma comment (lib,"bass.lib")
#include <stdlib.h>
#include <string.h>
#include <mmsystem.h>
#include "3rdparty/gb_math.h"
#include "3rdparty/rocket/sync.h"
#include<iostream>
#include<vector>
#include<cstring>
#include<fstream>
#include <string>
#include <sstream>  

using namespace std;
#define MAX_VERTEX_BUFFER 512 * 1024
#define MAX_ELEMENT_BUFFER 128 * 1024

struct glsl2configmap
{
	char name[100];
	int frag_number;
	int program_num;
	float val;
	float min;
	float max;
	float inc;
};
std::vector<glsl2configmap>shaderconfig_map;

struct shader_id
{
	int fsid;
	int vsid;
	unsigned int pid;
	bool compiled;
};

struct FBOELEM {
	GLuint fbo;
	GLuint depthbuffer;
	GLuint texture;
	GLuint depthtexture;
	GLint status;
};

static struct sync_device *device = NULL;
#if !defined(SYNC_PLAYER)
static struct sync_cb cb;
#endif
int rocket_connected = 0;
HSTREAM music_stream = NULL;
float rps = 5.0f;
int audio_is_playing = 1;
shader_id raymarch_shader = { 0 };
static float sceneTime = 0;
drfsw_context* context = NULL;

GLuint scene_vao, scene_texture;

int row_to_ms_round(int row, float rps)
{
	const float newtime = ((float)(row)) / rps;
	return (int)(floor(newtime + 0.5f));
}

float ms_to_row_f(float time_ms, float rps)
{
	const float row = rps * time_ms;
	return row;
}

int ms_to_row_round(int time_ms, float rps)
{
	const float r = ms_to_row_f(time_ms, rps);
	return (int)(floor(r + 0.5f));
}

#if !defined(SYNC_PLAYER)

static void xpause(void* data, int flag)
{
	(void)data;

	if (flag)
	{
		BASS_ChannelPause(music_stream);
		audio_is_playing = 0;
	}
		
	else
	{
		audio_is_playing = 1;
		BASS_ChannelPlay(music_stream, false);
	}
	
}

static void xset_row(void* data, int row)
{
	int newtime_ms = row_to_ms_round(row, rps);
	sceneTime = newtime_ms;
	if (BASS_ChannelIsActive(music_stream) != BASS_ACTIVE_STOPPED)
	BASS_ChannelSetPosition(music_stream,BASS_ChannelSeconds2Bytes(music_stream, sceneTime),BASS_POS_BYTE);
	(void)data;
}

static int xis_playing(void* data)
{
	(void)data;
	return audio_is_playing;
}

#endif //!SYNC_PLAYER

int rocket_init(const char* prefix)
{
	device = sync_create_device(prefix);
	if (!device)
	{
		printf("Unable to create rocketDevice\n");
		return 0;
	}

#if !defined( SYNC_PLAYER )
	cb.is_playing = xis_playing;
	cb.pause = xpause;
	cb.set_row = xset_row;

	if (sync_connect(device, "localhost", SYNC_DEFAULT_PORT))
	{
		printf("Rocket failed to connect\n");
		return 0;
	}
#endif

	printf("Rocket connected.\n");

	return 1;
}



void update_rocket()
{
	if (rocket_connected)
	{
		float row_f = ms_to_row_f(sceneTime, rps);
		if (sync_update(device, (int)floor(row_f), &cb, 0))
		{
			rocket_connected = sync_connect(device, "localhost", SYNC_DEFAULT_PORT);
		}

		if (rocket_connected)
		{
			for (int i = 0; i < shaderconfig_map.size(); i++)
			{
				if (strstr(shaderconfig_map[i].name, "_rkt") != NULL)
				{
					string shit = shaderconfig_map[i].name;
					shit = shit.substr(0, shit.size() - 4);
					const sync_track *track = sync_get_track(device, shit.c_str());
					shaderconfig_map[i].val = sync_get_val(track, row_f);
				}
			}
		}

	}
	
}

void glsl_to_config(shader_id prog, char *shader_path)
{
	
		vector<string>lines;
		lines.clear();
		ifstream openFile(shader_path);
		string stringToStore; //string to store file line
		while (getline(openFile, stringToStore)) { //checking for existence of file
			lines.push_back(stringToStore);
		}
		openFile.close(); //closes file after done

		//convert GLSL uniforms to variables
		int total = -1;
		glGetProgramiv(prog.fsid, GL_ACTIVE_UNIFORMS, &total);
		for (int i = 0; i < total; ++i) {
			int name_len = -1, num = -1;
			GLenum type = GL_ZERO;
			char name[100] = { 0 };
			glGetActiveUniform(prog.fsid, GLuint(i), sizeof(name) - 1,
				&name_len, &num, &type, name);
			name[name_len] = 0;
			if (type == GL_FLOAT) {
				for (int j= 0; j < lines.size(); j++)
				{
					string shit = "uniform float ";shit += name;
					if (strstr(lines[j].c_str(),name))
					{
						//Nuklear (user controllable)
						std::size_t found = lines[j].rfind("//");
						if (found != std::string::npos)
						{
							string shit2 = lines[j].substr(found + 2, lines[j].length());
							stringstream parse1(shit2);
							float min = 0., max = 0., inc = 0.;
							parse1 >> min >> max >> inc;
							glsl2configmap subObj = { 0 };
							strcpy(subObj.name, name);
							subObj.frag_number = prog.fsid;
							subObj.program_num = prog.pid;
							subObj.inc = inc;
							subObj.min = min;
							subObj.max = max;
							shaderconfig_map.push_back(subObj);
						}
						//GNU Rocket (user scriptable)
						else if(lines[j].rfind("_rkt") != std::string::npos && rocket_connected)
						{
								glsl2configmap subObj = { 0 };
								strcpy(subObj.name, name);
								subObj.frag_number = prog.fsid;
								subObj.program_num = prog.pid;
								float val = 0.0;
								subObj.inc = val;
								subObj.min = val;
								subObj.max = val;
								shaderconfig_map.push_back(subObj);
						}
					}

				}
			}
		}
}

shader_id initShader(shader_id shad,const char *vsh, const char *fsh)
{
	shad.compiled = true;
	shad.vsid = glCreateShaderProgramv(GL_VERTEX_SHADER, 1, &vsh);
	shad.fsid = glCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, &fsh);
	glGenProgramPipelines(1, &shad.pid);
	glBindProgramPipeline(shad.pid);
	glUseProgramStages(shad.pid, GL_VERTEX_SHADER_BIT, shad.vsid);
	glUseProgramStages(shad.pid, GL_FRAGMENT_SHADER_BIT, shad.fsid);
#ifdef DEBUG
	int		result;
	char    info[1536];
	glGetProgramiv(shad.vsid, GL_LINK_STATUS, &result); glGetProgramInfoLog(shad.vsid, 1024, NULL, (char *)info); if (!result){ goto fail; }
	glGetProgramiv(shad.fsid, GL_LINK_STATUS, &result); glGetProgramInfoLog(shad.fsid, 1024, NULL, (char *)info); if (!result){ goto fail; }
	glGetProgramiv(shad.pid, GL_LINK_STATUS, &result); glGetProgramInfoLog(shad.pid, 1024, NULL, (char *)info); if (!result){ goto fail; }
#endif
	
	glBindProgramPipeline(0);
	shad.compiled = true;
	return shad;
fail:
	{
		shad.compiled = false;
		glDeleteProgram(shad.fsid);
		glDeleteProgram(shad.vsid);
		glBindProgramPipeline(0);
		glDeleteProgramPipelines(1, &shad.pid);
		return shad;
	}
}



#define GLSL(src) #src

const char vertex_source[] =
"#version 330\n"
"layout(location = 0) in vec4 vposition;\n"
"layout(location = 1) in vec2 vtexcoord;\n"
"out vec2 ftexcoord;\n"
"void main() {\n"
"   ftexcoord = vtexcoord;\n"
"   gl_Position =vec4(vposition.xy, 0.0f, 1.0f);\n"
"}\n";

void init_raymarch()
{

	// get texture uniform location


	// vao and vbo handle
	GLuint vbo;

	// generate and bind the vao
	glGenVertexArrays(1, &scene_vao);
	glBindVertexArray(scene_vao);

	// generate and bind the vertex buffer object, to be used with VAO
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	// data for a fullscreen quad (this time with texture coords)
	// we use the texture coords for whenever a LUT is loaded
	typedef struct
	{
		float   x;
		float   y;
		float   z;
		float   u;
		float   v;
	} VBufVertex;
	VBufVertex vertexData[] = {
		//  X     Y     Z           U     V     
		1.0f, 1.0f, 0.0f, 1.0f, 1.0f, // vertex 0
		-1.0f, 1.0f, 0.0f, 0.0f, 1.0f, // vertex 1
		1.0f, -1.0f, 0.0f, 1.0f, 0.0f, // vertex 2
		1.0f, -1.0f, 0.0f, 1.0f, 0.0f, // vertex 2
		-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, // vertex 3
		-1.0f, 1.0f, 0.0f, 0.0f, 1.0f, // vertex 1
	}; // 6 vertices with 5 components (floats) each
	// fill with data
	glBufferData(GL_ARRAY_BUFFER, sizeof(VBufVertex) * 6, vertexData, GL_STATIC_DRAW);
	// set up generic attrib pointers
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(VBufVertex), (void*)offsetof(VBufVertex, x));
	glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VBufVertex), (void*)offsetof(VBufVertex, u));
	// "unbind" voa
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void draw_raymarch(float time, shader_id program, int xres, int yres){
	glBindProgramPipeline(program.pid);
	float fparams[4] = { xres, yres, time, 0.0 };
	glProgramUniform4fv(program.fsid, 1, 1, fparams);
	for (int i = 0; i < shaderconfig_map.size(); i++)
	{
		if (shaderconfig_map[i].program_num = program.pid)
		{
			int uniform_loc = glGetUniformLocation(program.fsid, shaderconfig_map[i].name);
			glProgramUniform1f(program.fsid, uniform_loc, shaderconfig_map[i].val);
		}
			
	}
	// bind the vao
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindVertexArray(scene_vao);
	// draw
	glDrawArrays(GL_TRIANGLES, 0, 6);
	glBindVertexArray(0);
	glBindProgramPipeline(0);
	glDisable(GL_BLEND);
}

void PezHandleMouse(int x, int y, int action) { }

void PezUpdate(unsigned int elapsedMilliseconds) {
	if (BASS_ChannelIsActive(music_stream) != BASS_ACTIVE_STOPPED)
	{
		if (audio_is_playing)
		{
			QWORD len = BASS_ChannelGetPosition(music_stream, BASS_POS_BYTE); // the length in bytes
			sceneTime = BASS_ChannelBytes2Seconds(music_stream, len);
		}
	}
	else
	{
		if (rocket_connected)
		{
			if (audio_is_playing)
			{
				sceneTime += elapsedMilliseconds * 0.001;
			}
			else
			{
				return;
			}
		}
		sceneTime += elapsedMilliseconds * 0.001;
	}
}

 char* getFileNameFromPath(char* path)
 {
	 for (size_t i = strlen(path) - 1; i; i--)
	 {
		 if (path[i] == '/')
		 {
			 return &path[i + 1];
		 }
	 }
	 return path;
 }
unsigned long last_load=0;

 void recompile_shader(char* path)
 {

	 if (strcmp(getFileNameFromPath(path), "raymarch.glsl") == 0)
	 {
		
		 unsigned long load = timeGetTime();
		 if (load-last_load > 200) { //take into account actual shader recompile time
			 shaderconfig_map.clear();
			 Sleep(100);
			 if (glIsProgramPipeline(raymarch_shader.pid)) {
				 glDeleteProgram(raymarch_shader.fsid);
				 glDeleteProgram(raymarch_shader.vsid);
				 glBindProgramPipeline(0);
				 glDeleteProgramPipelines(1, &raymarch_shader.pid);
			 }
			 raymarch_shader = { 0 };
			 raymarch_shader.compiled = false;
			 size_t sizeout = 0;
			 char* pix_shader = dr_open_and_read_text_file(path, &sizeout);
			 if (pix_shader) {
				 raymarch_shader = initShader(raymarch_shader, vertex_source, (const char*)pix_shader);
				 glsl_to_config(raymarch_shader, "raymarch.glsl");
				 dr_free_file_data(pix_shader);
			 }
			
		 }
		 last_load = timeGetTime();
	 }

	 if (strcmp(getFileNameFromPath(path), "post.glsl") == 0)
	 {

	 }
 }

struct nk_color background;
int action = 0;
bool seek = false;



double pRound2(double number)
{
	return (number >= 0) ? (int)(number + 0.5) : (int)(number - 0.5);
}

double pround(double x, int precision)
{
	if (x == 0.)
		return x;
	int ex = floor(log10(abs(x))) - precision + 1;
	double div = pow(10, ex);
	return floor(x / div + 0.5) * div;
}
#include <Commdlg.h>
#include <windows.h>
char *get_file(void) {
	OPENFILENAME    ofn;
	char     filename[4096] = { 0 };
	static const char   filter[] =
		"(*.*)\0"       "*.*\0"
		"\0"            "\0";

	filename[0] = 0;
	memset(&ofn, 0, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFilter = filter;
	ofn.nFilterIndex = 1;
	ofn.lpstrFile = filename;
	ofn.nMaxFile = sizeof(filename);
	ofn.lpstrTitle = "Select the input audio file";
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_LONGNAMES | OFN_EXPLORER | OFN_HIDEREADONLY;

	printf("- %s\n", ofn.lpstrTitle);
	if (!GetOpenFileName(&ofn)) return NULL;
	return(filename);
}


void gui()
{
	static QWORD len;
	static double time;
	if (ctx)
	{
		if (nk_begin(ctx, "Timeline", nk_rect(30, 520, 530, 160),
			NK_WINDOW_BORDER | NK_WINDOW_MOVABLE |
			NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE))
		{
			nk_layout_row_static(ctx, 30, 100, 4);
			if (nk_button_label(ctx, "Load/Unload"))
			{
			
				char *file = get_file();
				if (file)
				{
					if (BASS_ChannelIsActive(music_stream) != BASS_ACTIVE_STOPPED)	BASS_StreamFree(music_stream);
					if (music_stream = BASS_StreamCreateFile(FALSE, file, 0, 0, BASS_STREAM_AUTOFREE))
					{
						len = BASS_ChannelGetLength(music_stream, BASS_POS_BYTE); // the length in bytes
						time = BASS_ChannelBytes2Seconds(music_stream, len);
						BASS_ChannelPlay(music_stream, TRUE);
						sceneTime = 0;
					}
				}
				else
				{
					if (BASS_ChannelIsActive(music_stream) != BASS_ACTIVE_STOPPED)
					{
						BASS_StreamFree(music_stream);
					}
				}
			}
				
			if (nk_button_label(ctx, "Pause/Resume"))
			{
				if (BASS_ChannelIsActive(music_stream) == BASS_ACTIVE_PLAYING)
				{
					BASS_ChannelPause(music_stream);
				}
				else
				{
					BASS_ChannelPlay(music_stream,FALSE);
				}
				
			}
			if (nk_button_label(ctx, "Rewind"))
			{
				BASS_ChannelSetPosition(music_stream,0,BASS_POS_INEXACT);
				sceneTime = 0;
			}
			if (BASS_ChannelIsActive(music_stream) != BASS_ACTIVE_STOPPED)
			{
				float max = time;
				nk_layout_row_dynamic(ctx, 25, 1);
				char label1[100] = { 0 };
				sprintf(label1, "Progress: %.2f / %.2f seconds", sceneTime,max);
				nk_label(ctx, label1, NK_TEXT_LEFT);
				nk_layout_row_static(ctx, 30, 500, 2);

				seek = nk_slider_float(ctx, 0, (float*)&sceneTime, max, 0.1);
				if (seek)
				{
					BASS_ChannelSetPosition(
						music_stream,
						BASS_ChannelSeconds2Bytes(music_stream, sceneTime),
						BASS_POS_BYTE
					);
				}
			}
			else
			{
				int max = 300;
				nk_layout_row_dynamic(ctx, 25, 1);
				char label1[100] = { 0 };
				sprintf(label1, "Progress: %.2f seconds", sceneTime);
				nk_label(ctx, label1, NK_TEXT_LEFT);
				nk_layout_row_static(ctx, 30, 500, 2);

				seek = nk_slider_float(ctx, 0, (float*)&sceneTime, max, 0.1);
			}
			
		}
	
		nk_end(ctx);

		if (nk_begin(ctx, "Raymarch Uniforms", nk_rect(1000, 30, 300, 200),
			NK_WINDOW_BORDER | NK_WINDOW_MOVABLE |
			NK_WINDOW_MINIMIZABLE | NK_WINDOW_TITLE))
		{
			for (int i = 0; i < shaderconfig_map.size(); i++) {
				if (strstr(shaderconfig_map[i].name, "_rkt") == NULL) {
					nk_layout_row_dynamic(ctx, 25, 1);
					char label1[100] = { 0 };
					sprintf(label1, "%s: %.2f", shaderconfig_map[i].name, shaderconfig_map[i].val);
					nk_label(ctx, label1, NK_TEXT_LEFT);
					nk_layout_row_static(ctx, 30, 250, 2);
					nk_slider_float(ctx, shaderconfig_map[i].min, &shaderconfig_map[i].val, shaderconfig_map[i].max, 0.1);
				}
			}
		}
	   nk_end(ctx);

	
		
	}

	
}

void PezRender()
{
	
	drfsw_event e;
	if (drfsw_peek_event(context, &e))
	{
		switch (e.type)
		{
		case drfsw_event_type_updated: recompile_shader(e.absolutePath); break;
		default: break;
		}
	}

	float bg[4];
	nk_color_fv(bg, background);
	glClear(GL_COLOR_BUFFER_BIT);
	glClearColor(bg[0], bg[1], bg[2], bg[3]);
	
	gui();

	if (seek)
	{
		sceneTime = floor(sceneTime);
	}
	

	update_rocket();

	if (raymarch_shader.compiled)
	{
		draw_raymarch(sceneTime, raymarch_shader, 1280, 720);
		
	}
	
	nk_pez_render(NK_ANTI_ALIASING_ON, MAX_VERTEX_BUFFER, MAX_ELEMENT_BUFFER);

}

const char* PezInitialize(int width, int height)
{
	BASS_Init(-1, 44100, 0, NULL, NULL);

	rocket_connected = rocket_init("rocket");
	context = drfsw_create_context();
	TCHAR path[512] = { 0 };
	dr_get_executable_directory_path(path, sizeof(path));
	dr_set_current_directory(path);
	drfsw_add_directory(context, path);
	size_t sizeout;
	char* pix_shader = dr_open_and_read_text_file("raymarch.glsl", &sizeout);
	if (pix_shader == NULL)return NULL;
	raymarch_shader = initShader(raymarch_shader,vertex_source, (const char*)pix_shader);
	init_raymarch();
	shaderconfig_map.clear();
	glsl_to_config(raymarch_shader, "raymarch.glsl");
	free(pix_shader);
	background = nk_rgb(28, 48, 62);

    return "Shader Lathe v0.0";
}