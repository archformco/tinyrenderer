#pragma once
#include <iostream>
#include <map>
#include <vector>
#include <limits>

using namespace std;

#include "model.h"
#include "our_gl.h"


/* * * *  GLOBAL FUNCTION DEFINE  * * * */

map<string, vector<string>> Getopt(const int argc, char* const argv[]);
bool IsLetter(const string& str);
bool IsNumber(const string& str);
int TryStoi(const string& str);
double TryStod(const string& str);
void PrintCmds(const map<string, vector<string>>& Cmds);
bool InitGlobalParam(map<string, vector<string>>& Cmds);
void PrintGlobal();
void ShowMenu();
bool CreateTgaFiles(std::string filename);


/* * * *  GLOBAL DATA STRUCTURE  * * * */

static string obj = "";     // tinyrender obj file

static int width;       // output image size
static int height;
static vec3 light_dir;  // light source
static vec3 eye;        // camera position
static vec3 center;     // camera direction
static vec3 up;         // camera up vector

extern mat<4, 4> ModelView; // "OpenGL" state matrices
extern mat<4, 4> Projection;

static vector<uint8_t> new_bgra(4, 0);

struct Shader : IShader {
    const Model &model;
    vec3 uniform_l;       // light direction in view coordinates
    mat<2,3> varying_uv;  // triangle uv coordinates, written by the vertex shader, read by the fragment shader
    mat<3,3> varying_nrm; // normal per vertex to be interpolated by FS
    mat<3,3> view_tri;    // triangle in view coordinates

    Shader(const Model &m) : model(m) {
        uniform_l = proj<3>((ModelView*embed<4>(light_dir, 0.))).normalized(); // transform the light vector to view coordinates
    }

    virtual void vertex(const int iface, const int nthvert, vec4& gl_Position) {
        varying_uv.set_col(nthvert, model.uv(iface, nthvert));
        varying_nrm.set_col(nthvert, proj<3>((ModelView).invert_transpose()*embed<4>(model.normal(iface, nthvert), 0.)));
        gl_Position= ModelView*embed<4>(model.vert(iface, nthvert));
        view_tri.set_col(nthvert, proj<3>(gl_Position));
        gl_Position = Projection*gl_Position;
    }

    virtual bool fragment(const vec3 bar, TGAColor &gl_FragColor) {
        vec3 bn = (varying_nrm*bar).normalized(); // per-vertex normal interpolation
        vec2 uv = varying_uv*bar; // tex coord interpolation

        // for the math refer to the tangent space normal mapping lecture
        // https://github.com/ssloy/tinyrenderer/wiki/Lesson-6bis-tangent-space-normal-mapping
        mat<3,3> AI = mat<3,3>{ {view_tri.col(1) - view_tri.col(0), view_tri.col(2) - view_tri.col(0), bn} }.invert();
        vec3 i = AI * vec3{varying_uv[0][1] - varying_uv[0][0], varying_uv[0][2] - varying_uv[0][0], 0};
        vec3 j = AI * vec3{varying_uv[1][1] - varying_uv[1][0], varying_uv[1][2] - varying_uv[1][0], 0};
        mat<3,3> B = mat<3,3>{ {i.normalized(), j.normalized(), bn} }.transpose();

        vec3 n = (B * model.normal(uv)).normalized(); // transform the normal from the texture to the tangent space
        double diff = std::max(0., n*uniform_l); // diffuse light intensity
        vec3 r = (n*(n*uniform_l)*2 - uniform_l).normalized(); // reflected light direction, specular mapping is described here: https://github.com/ssloy/tinyrenderer/wiki/Lesson-6-Shaders-for-the-software-renderer
        double spec = std::pow(std::max(-r.z, 0.), 5+sample2D(model.specular(), uv)[0]); // specular intensity, note that the camera lies on the z-axis (in view), therefore simple -r.z

        TGAColor c = sample2D(model.diffuse(), uv);
        for (int i : {0,1,2})
            gl_FragColor[i] = std::min<int>(10 + c[i]*(diff + spec), 255); // (a bit of ambient light, diff + spec), clamp the result

        return false; // the pixel is not discarded
    }
};


/* * * *  MAIN FUNCTION  * * * */

int main(int argc, char** argv)
{
    map<std::string, vector<std::string>> Cmds = Getopt(argc, argv);

    PrintCmds(Cmds);

    if (!InitGlobalParam(Cmds))
    {
        cout << "The program is missing a required parameter." << endl << endl;
        ShowMenu();
        return -1;
    }

    CreateTgaFiles(obj);

    TGAImage framebuffer(width, height, TGAImage::RGB, new_bgra); // the output image
    lookat(eye, center, up);                            // build the ModelView matrix
    viewport(width/8, height/8, width*3/4, height*3/4); // build the Viewport matrix
    projection((eye-center).norm());                    // build the Projection matrix
    std::vector<double> zbuffer(width*height, std::numeric_limits<double>::max());

    for (int m=1; m<argc; m++) { // iterate through all input objects
        Model model(argv[m]);
        Shader shader(model);
        for (int i=0; i<model.nfaces(); i++) { // for every triangle
            vec4 clip_vert[3]; // triangle coordinates (clip coordinates), written by VS, read by FS
            for (int j : {0,1,2})
                shader.vertex(i, j, clip_vert[j]); // call the vertex shader for each triangle vertex
            triangle(clip_vert, shader, framebuffer, zbuffer); // actual rasterization routine call
        }
    }
    framebuffer.write_tga_file("framebuffer.tga");
    return 0;
}


/* * * *  GLOBAL FUNCTION IMPLEMENT  * * * */

map<string, vector<string>> Getopt(const int argc, char* const argv[])
{
    map<string, vector<string>> retCmdValueMap;
    string cur = "";
    int ops = 1;

    while (ops < argc)
    {
        string str = argv[ops++];
        if ("" != str)
        {
            if ('-' == str[0])
            {
                if (1 < str.length())
                {
                    string subStr = str.substr(1);
                    if (IsLetter(subStr))
                    {
                        retCmdValueMap.insert({ subStr, {} });
                        cur = subStr;
                    }
                    else
                    {
                        if ("" != cur && IsNumber(str))
                        {
                            retCmdValueMap[cur].push_back(str);
                        }
                    }
                }
            }
            else
            {
                if ("" != cur)
                {
                    retCmdValueMap[cur].push_back(str);
                }
            }
        }
    }
    return retCmdValueMap;
}

bool IsLetter(const string& str)
{
    bool flag = true;
    for (auto ch : str)
    {
        if (('A' <= ch && ch <= 'Z') || ('a' <= ch && ch <= 'z') || ('.' == ch) || ('_' == ch))
        {
            flag = true;
        }
        else
        {
            flag = false;
            break;
        }
    }
    return flag;
}

bool IsNumber(const string& str)
{
    bool flag = true;
    for (auto ch : str)
    {
        if (('0' <= ch && ch <= '9') || ('-' == ch))
        {
            flag = true;
        }
        else
        {
            flag = false;
            break;
        }
    }
    return flag;
}

int TryStoi(const string& str)
{
    try
    {
        return stoi(str);
    }
    catch (const std::exception&)
    {
        return 0;
    }
}

double TryStod(const string& str)
{
    try
    {
        return stod(str);
    }
    catch (const std::exception&)
    {
        return 0.0;
    }
}

void PrintCmds(const map<string, vector<string>>& Cmds)
{
    for (auto val : Cmds)
    {
        cout << "Cmds[ " << val.first << " ] = [ ";
        for (auto val2 : val.second)
        {
            cout << val2 << " ";
        }
        cout << "]" << endl;
    }
}

bool InitGlobalParam(map<string, vector<string>>& Cmds)
{
    vector<bool> flags(8, false);

    if (Cmds.end() != Cmds.find("obj"))
    {
        if (1 == Cmds["obj"].size())
        {
            obj = Cmds["obj"][0];
            flags[0] = true;
        }
    }

    if (Cmds.end() != Cmds.find("width"))
    {
        if (1 == Cmds["width"].size())
        {
            width = TryStoi(Cmds["width"][0]);
            flags[1] = true;
        }
    }

    if (Cmds.end() != Cmds.find("height"))
    {
        if (1 == Cmds["height"].size())
        {
            height = TryStoi(Cmds["height"][0]);
            flags[2] = true;
        }
    }

    if (Cmds.end() != Cmds.find("light_dir"))
    {
        if (3 == Cmds["light_dir"].size())
        {
            light_dir[0] = TryStod(Cmds["light_dir"][0]);
            light_dir[1] = TryStod(Cmds["light_dir"][1]);
            light_dir[2] = TryStod(Cmds["light_dir"][2]);
            flags[3] = true;
        }
    }

    if (Cmds.end() != Cmds.find("eye"))
    {
        if (3 == Cmds["eye"].size())
        {
            eye[0] = TryStod(Cmds["eye"][0]);
            eye[1] = TryStod(Cmds["eye"][1]);
            eye[2] = TryStod(Cmds["eye"][2]);
            flags[4] = true;
        }
    }

    if (Cmds.end() != Cmds.find("center"))
    {
        if (3 == Cmds["center"].size())
        {
            center[0] = TryStod(Cmds["center"][0]);
            center[1] = TryStod(Cmds["center"][1]);
            center[2] = TryStod(Cmds["center"][2]);
            flags[5] = true;
        }
    }

    if (Cmds.end() != Cmds.find("up"))
    {
        if (3 == Cmds["up"].size())
        {
            up[0] = TryStod(Cmds["up"][0]);
            up[1] = TryStod(Cmds["up"][1]);
            up[2] = TryStod(Cmds["up"][2]);
            flags[6] = true;
        }
    }

    if (Cmds.end() != Cmds.find("new_bgra"))
    {
        if (4 == Cmds["new_bgra"].size())
        {
            new_bgra[0] = TryStod(Cmds["new_bgra"][0]);
            new_bgra[1] = TryStod(Cmds["new_bgra"][1]);
            new_bgra[2] = TryStod(Cmds["new_bgra"][2]);
            new_bgra[3] = TryStod(Cmds["new_bgra"][3]);
            flags[7] = true;
        }
    }

    return flags[0] && flags[1] && flags[2] && flags[3] && flags[4] && flags[5] && flags[6] && flags[7];
}

void PrintGlobal()
{
    cout << "obj = " << obj << endl;
    cout << "width = " << width << endl;
    cout << "height = " << height << endl;

    cout << "light_dir =";
    for (int i = 0; i < 3; ++i)
    {
        cout << " " << light_dir[i];
    }
    cout << endl;

    cout << "eye =";
    for (int i = 0; i < 3; ++i)
    {
        cout << " " << eye[i];
    }
    cout << endl;

    cout << "center =";
    for (int i = 0; i < 3; ++i)
    {
        cout << " " << center[i];
    }
    cout << endl;

    cout << "up =";
    for (int i = 0; i < 3; ++i)
    {
        cout << " " << up[i];
    }
    cout << endl;
}

void ShowMenu()
{
    std::cout << "Input the params :" << std::endl;
    std::cout << "\t" << "-obj " << 1 << std::endl;
    std::cout << "\t" << "-width " << 1 << std::endl;
    std::cout << "\t" << "-height " << 1 << std::endl;
    std::cout << "\t" << "-light_dir " << 3 << std::endl;
    std::cout << "\t" << "-eye " << 3 << std::endl;
    std::cout << "\t" << "-center " << 3 << std::endl;
    std::cout << "\t" << "-up " << 3 << std::endl;
    std::cout << "\t" << "-new_bgra " << 4 << std::endl << endl;
}

bool CreateTgaFiles(std::string filename)
{
    std::string obj_diffuse = "_diffuse.tga";
    std::string obj_nm_tangent = "_nm_tangent.tga";
    std::string obj_spece = "_spec.tga";
    ofstream out1, out2, out3;

    size_t dot = obj.find_last_of(".");
    std::string fileNameNoSuffix = obj.substr(0, dot);
    if (dot == std::string::npos)
        return -1;

    // make obj_diffuse.tga
    obj_diffuse = obj.substr(0, dot) + obj_diffuse;
    out1.open(obj_diffuse, 'r');
    out1.clear();
    out1.close();

    // make obj_nm_tangent.tga
    obj_nm_tangent = obj.substr(0, dot) + obj_nm_tangent;
    out2.open(obj_nm_tangent, 'r');
    out2.clear();
    out2.close();

    // make obj_spece.tga
    obj_spece = obj.substr(0, dot) + obj_spece;
    out3.open(obj_spece, 'r');
    out3.clear();
    out3.close();
}