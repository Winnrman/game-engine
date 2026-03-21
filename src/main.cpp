#define STB_IMAGE_IMPLEMENTATION
#include "../stb_image.h"

#include <iostream>
#include <vector>
#include <cmath>
#include <glad/glad.h>
#include <GLFW/glfw3.h>

// -----------------------------------------------------------------------
// Vec3 / Mat4 / Camera — unchanged from before
// -----------------------------------------------------------------------
struct Vec3 {
    float x, y, z;
    Vec3 operator-(const Vec3& r) const { return {x-r.x, y-r.y, z-r.z}; }
    Vec3 operator+(const Vec3& r) const { return {x+r.x, y+r.y, z+r.z}; }
    Vec3 operator*(float s)       const { return {x*s,   y*s,   z*s};   }
};
float Dot(const Vec3& a, const Vec3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3  Cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
float Clamp(float v, float lo, float hi) { return v<lo?lo:v>hi?hi:v; }
Vec3  Normalize(const Vec3& v) { float l=sqrtf(Dot(v,v)); return {v.x/l,v.y/l,v.z/l}; }

struct Mat4 { float m[16]; };
inline float& At(Mat4& M,int c,int r)      { return M.m[c*4+r]; }
inline float  At(const Mat4& M,int c,int r){ return M.m[c*4+r]; }
Mat4 Mat4Identity(){ Mat4 r={}; At(r,0,0)=At(r,1,1)=At(r,2,2)=At(r,3,3)=1; return r; }
Mat4 operator*(const Mat4& a,const Mat4& b){
    Mat4 r={};
    for(int c=0;c<4;c++) for(int row=0;row<4;row++)
        At(r,c,row)=At(a,0,row)*At(b,c,0)+At(a,1,row)*At(b,c,1)
                   +At(a,2,row)*At(b,c,2)+At(a,3,row)*At(b,c,3);
    return r;
}
Mat4 perspective(float fov,float aspect,float n,float f){
    Mat4 r={}; float a=1.f/tanf(fov/2.f);
    At(r,0,0)=a/aspect; At(r,1,1)=a;
    At(r,2,2)=(f+n)/(n-f); At(r,2,3)=-1.f;
    At(r,3,2)=(2.f*f*n)/(n-f); return r;
}
Mat4 LookAt(const Vec3& eye,const Vec3& target,const Vec3& up){
    Vec3 f=Normalize(target-eye),r=Normalize(Cross(f,up)),u=Cross(r,f);
    Mat4 v=Mat4Identity();
    At(v,0,0)=r.x; At(v,1,0)=r.y; At(v,2,0)=r.z;
    At(v,0,1)=u.x; At(v,1,1)=u.y; At(v,2,1)=u.z;
    At(v,0,2)=-f.x; At(v,1,2)=-f.y; At(v,2,2)=-f.z;
    At(v,3,0)=-Dot(r,eye); At(v,3,1)=-Dot(u,eye); At(v,3,2)=Dot(f,eye);
    return v;
}

struct Camera {
    Vec3  position, forward, up, target;
    float distance, fov, aspect, nearPlane, farPlane, yaw, pitch;
    Mat4 GetView() const {
        Vec3 t={position.x+forward.x,position.y+forward.y,position.z+forward.z};
        return LookAt(position,t,up);
    }
    Mat4 GetProjection() const { return perspective(fov,aspect,nearPlane,farPlane); }
};
Camera cam;

// -----------------------------------------------------------------------
// Callbacks
// -----------------------------------------------------------------------
static void error_callback(int e,const char* d){ fprintf(stderr,"Error: %s\n",d); }
static void key_callback(GLFWwindow* w,int key,int,int action,int){
    if(key==GLFW_KEY_ESCAPE&&action==GLFW_PRESS) glfwSetWindowShouldClose(w,GLFW_TRUE);
}
void scroll_callback(GLFWwindow*,double,double y){
    cam.distance-=(float)y;
    cam.distance=Clamp(cam.distance,2.f,100.f);
}

// -----------------------------------------------------------------------
// Shader helpers
// -----------------------------------------------------------------------
static void CheckShader(unsigned int s){
    int ok; char log[512];
    glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){glGetShaderInfoLog(s,512,nullptr,log);std::cerr<<"Shader: "<<log<<"\n";}
}
static void CheckProgram(unsigned int p){
    int ok; char log[512];
    glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){glGetProgramInfoLog(p,512,nullptr,log);std::cerr<<"Link: "<<log<<"\n";}
}

// -----------------------------------------------------------------------
// World
// -----------------------------------------------------------------------
static const int WORLD_W = 16;
static const int WORLD_D = 16;
static const int WORLD_H = 8;

static int heightMap[WORLD_W][WORLD_D];

static bool isSolid(int x,int y,int z){
    if(x<0||x>=WORLD_W||z<0||z>=WORLD_D||y<0||y>=WORLD_H) return false;
    return y < heightMap[x][z];
}

// -----------------------------------------------------------------------
// Mesh builder
// Only emit a face when the neighbor in that direction is air.
// This is the core Minecraft chunk optimization — buried faces cost nothing.
// Vertex layout: pos(3) + uv(2) + normal(3) + tangent(3) = 11 floats
// -----------------------------------------------------------------------
static void PushFace(std::vector<float>& verts,
                     Vec3 p0,Vec3 p1,Vec3 p2,Vec3 p3,
                     float u0,float v0,float u1,float v1,
                     float u2,float v2,float u3,float v3,
                     Vec3 normal,Vec3 tangent)
{
    auto push=[&](Vec3 p,float u,float v){
        verts.push_back(p.x); verts.push_back(p.y); verts.push_back(p.z);
        verts.push_back(u);   verts.push_back(v);
        verts.push_back(normal.x);  verts.push_back(normal.y);  verts.push_back(normal.z);
        verts.push_back(tangent.x); verts.push_back(tangent.y); verts.push_back(tangent.z);
    };
    push(p0,u0,v0); push(p1,u1,v1); push(p2,u2,v2);
    push(p2,u2,v2); push(p3,u3,v3); push(p0,u0,v0);
}

static void BuildWorldMesh(std::vector<float>& verts)
{
    verts.clear();
    for(int x=0;x<WORLD_W;x++){
        for(int z=0;z<WORLD_D;z++){
            for(int y=0;y<heightMap[x][z];y++){
                float fx=(float)x, fy=(float)y, fz=(float)z;

                // +X
                if(!isSolid(x+1,y,z))
                    PushFace(verts,
                        {fx+1,fy,  fz+1},{fx+1,fy,  fz  },{fx+1,fy+1,fz  },{fx+1,fy+1,fz+1},
                        0,0, 1,0, 1,1, 0,1,
                        {1,0,0},{0,0,1});

                // -X
                if(!isSolid(x-1,y,z))
                    PushFace(verts,
                        {fx,fy,  fz  },{fx,fy,  fz+1},{fx,fy+1,fz+1},{fx,fy+1,fz  },
                        0,0, 1,0, 1,1, 0,1,
                        {-1,0,0},{0,0,-1});

                // +Y (top)
                if(!isSolid(x,y+1,z))
                    PushFace(verts,
                        {fx,  fy+1,fz+1},{fx+1,fy+1,fz+1},{fx+1,fy+1,fz},{fx,fy+1,fz},
                        0,0, 1,0, 1,1, 0,1,
                        {0,1,0},{1,0,0});

                // -Y (bottom) — skip y==0, nobody sees the world floor
                if(y>0&&!isSolid(x,y-1,z))
                    PushFace(verts,
                        {fx,  fy,fz  },{fx+1,fy,fz  },{fx+1,fy,fz+1},{fx,fy,fz+1},
                        0,0, 1,0, 1,1, 0,1,
                        {0,-1,0},{1,0,0});

                // +Z
                if(!isSolid(x,y,z+1))
                    PushFace(verts,
                        {fx,  fy,  fz+1},{fx+1,fy,  fz+1},{fx+1,fy+1,fz+1},{fx,fy+1,fz+1},
                        0,0, 1,0, 1,1, 0,1,
                        {0,0,1},{1,0,0});

                // -Z
                if(!isSolid(x,y,z-1))
                    PushFace(verts,
                        {fx+1,fy,  fz},{fx,  fy,  fz},{fx,  fy+1,fz},{fx+1,fy+1,fz},
                        0,0, 1,0, 1,1, 0,1,
                        {0,0,-1},{-1,0,0});
            }
        }
    }
}

// -----------------------------------------------------------------------
// Texture loader
// -----------------------------------------------------------------------
static unsigned int LoadTexture(const char* path){
    unsigned int id;
    glGenTextures(1,&id);
    glBindTexture(GL_TEXTURE_2D,id);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    int w,h,ch;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data=stbi_load(path,&w,&h,&ch,0);
    if(data){
        GLenum fmt=(ch==4)?GL_RGBA:GL_RGB;
        glTexImage2D(GL_TEXTURE_2D,0,fmt,w,h,0,fmt,GL_UNSIGNED_BYTE,data);
        glGenerateMipmap(GL_TEXTURE_2D);
        std::cout<<"Loaded: "<<path<<"\n";
    } else {
        std::cerr<<"Failed: "<<path<<" — "<<stbi_failure_reason()<<"\n";
    }
    stbi_image_free(data);
    return id;
}

// -----------------------------------------------------------------------
// Shaders
// -----------------------------------------------------------------------
static const char* vertSrc = R"(
    #version 330 core
    layout(location=0) in vec3 aPos;
    layout(location=1) in vec2 aUV;
    layout(location=2) in vec3 aNormal;
    layout(location=3) in vec3 aTangent;

    out vec2 fragUV;
    out vec3 fragPos;
    out vec3 tLightPos;
    out vec3 tViewPos;
    out vec3 tFragPos;

    uniform mat4 uModel;
    uniform mat4 uView;
    uniform mat4 uProjection;
    uniform vec3 uLightPos;
    uniform vec3 uViewPos;

    void main(){
        vec4 worldPos = uModel * vec4(aPos,1.0);
        fragPos       = worldPos.xyz;
        fragUV        = aUV;
        gl_Position   = uProjection * uView * worldPos;

        mat3 normalMatrix = transpose(inverse(mat3(uModel)));
        vec3 T = normalize(normalMatrix * aTangent);
        vec3 N = normalize(normalMatrix * aNormal);
        T = normalize(T - dot(T,N)*N);
        vec3 B = cross(N,T);
        mat3 TBN = transpose(mat3(T,B,N));

        tLightPos = TBN * uLightPos;
        tViewPos  = TBN * uViewPos;
        tFragPos  = TBN * fragPos;
    }
)";

static const char* fragSrc = R"(
    #version 330 core
    in vec2 fragUV;
    in vec3 fragPos;
    in vec3 tLightPos;
    in vec3 tViewPos;
    in vec3 tFragPos;

    out vec4 FragColor;

    uniform sampler2D uTexture;
    uniform sampler2D uNormalMap;
    uniform vec3      uLightColor;

    void main(){
        vec3 texColor = texture(uTexture,  fragUV).rgb;
        vec3 norm     = normalize(texture(uNormalMap,fragUV).rgb * 2.0 - 1.0);

        vec3 ambient  = 0.15 * uLightColor;

        vec3  lightDir = normalize(tLightPos - tFragPos);
        float diff     = max(dot(norm,lightDir),0.0);
        vec3  diffuse  = diff * uLightColor;

        vec3  viewDir    = normalize(tViewPos - tFragPos);
        vec3  reflectDir = reflect(-lightDir,norm);
        float spec       = pow(max(dot(viewDir,reflectDir),0.0),32.0);
        vec3  specular   = 0.5 * spec * uLightColor;

        FragColor = vec4((ambient+diffuse+specular)*texColor,1.0);
    }
)";

// -----------------------------------------------------------------------
int main()
{
    // --- Height map: sine wave hills ---
    for(int x=0;x<WORLD_W;x++)
        for(int z=0;z<WORLD_D;z++){
            float wave = sinf(x*0.4f) * cosf(z*0.4f); // smooth -1..1
            heightMap[x][z] = 3 + (int)(wave * 2.0f);  // heights 1..5
        }

    // --- Build the entire world into one vertex buffer ---
    std::vector<float> worldVerts;
    BuildWorldMesh(worldVerts);
    int vertCount = (int)(worldVerts.size()/11);
    std::cout<<"World: "<<vertCount<<" verts, "<<vertCount/3<<" triangles\n";

    // --- GLFW init ---
    glfwSetErrorCallback(error_callback);
    if(!glfwInit()) return -1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window=glfwCreateWindow(1280,720,"Procedural World",nullptr,nullptr);
    if(!window){glfwTerminate();return -1;}
    glfwSetKeyCallback(window,key_callback);
    glfwSetScrollCallback(window,scroll_callback);
    glfwMakeContextCurrent(window);
    if(!gladLoadGL()){std::cerr<<"GLAD failed\n";return -1;}

    // --- VAO / VBO ---
    unsigned int VAO,VBO;
    glGenVertexArrays(1,&VAO);
    glGenBuffers(1,&VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER,VBO);
    glBufferData(GL_ARRAY_BUFFER,worldVerts.size()*sizeof(float),worldVerts.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,11*sizeof(float),(void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,11*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,3,GL_FLOAT,GL_FALSE,11*sizeof(float),(void*)(5*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3,3,GL_FLOAT,GL_FALSE,11*sizeof(float),(void*)(8*sizeof(float)));
    glEnableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER,0);
    glBindVertexArray(0);

    // --- Shaders ---
    unsigned int vs=glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs,1,&vertSrc,nullptr); glCompileShader(vs); CheckShader(vs);
    unsigned int fs=glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs,1,&fragSrc,nullptr); glCompileShader(fs); CheckShader(fs);
    unsigned int prog=glCreateProgram();
    glAttachShader(prog,vs); glAttachShader(prog,fs);
    glLinkProgram(prog); CheckProgram(prog);
    glDeleteShader(vs); glDeleteShader(fs);

    // --- Textures ---
    unsigned int texDiffuse=LoadTexture("assets/textures/crate.png");
    unsigned int texNormal =LoadTexture("assets/textures/crate_normal.png");
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog,"uTexture"),  0);
    glUniform1i(glGetUniformLocation(prog,"uNormalMap"),1);

    // --- Camera: positioned above world center, looking down ---
    cam.up        = {0,1,0};
    cam.fov       = 3.14159f/3.0f;
    cam.aspect    = 1280.f/720.f;
    cam.nearPlane = 0.1f;
    cam.farPlane  = 200.f;
    cam.target    = {WORLD_W/2.f, 4.f, WORLD_D/2.f}; // sit above terrain
    cam.distance  = 28.f;
    cam.yaw       = 0.4f;
    cam.pitch     = 0.9f; // start looking steeply down so world is visible
    cam.forward   = {0,0,-1};
    cam.position  = {0,10,0};

    // Sun position — high and to the side
    Vec3 lightPos   = {WORLD_W/2.f+10.f, 20.f, WORLD_D/2.f+10.f};
    Vec3 lightColor = {1.f,0.98f,0.9f}; // slightly warm sunlight

    Mat4 model=Mat4Identity();

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);  // back-face culling — free performance
    glCullFace(GL_BACK);

    double lastX=0,lastY=0;
    bool orbiting=false;
    float lastTime=0;

    while(!glfwWindowShouldClose(window))
    {
        float now=(float)glfwGetTime();
        float dt = now - lastTime;
        lastTime=now;
        glfwPollEvents();

        // --- WASD / Space / Shift: move the orbit TARGET ---
        // Moves the point the camera orbits around, dragging the camera with it.
        {
            float speed = 8.0f * dt;
            Vec3 flat  = Normalize({cam.forward.x, 0.f, cam.forward.z});
            Vec3 right = Normalize(Cross(flat, {0.f,1.f,0.f}));

            if(glfwGetKey(window, GLFW_KEY_W)          == GLFW_PRESS) cam.target = cam.target + flat  * speed;
            if(glfwGetKey(window, GLFW_KEY_S)          == GLFW_PRESS) cam.target = cam.target - flat  * speed;
            if(glfwGetKey(window, GLFW_KEY_A)          == GLFW_PRESS) cam.target = cam.target - right * speed;
            if(glfwGetKey(window, GLFW_KEY_D)          == GLFW_PRESS) cam.target = cam.target + right * speed;
            if(glfwGetKey(window, GLFW_KEY_SPACE)      == GLFW_PRESS) cam.target.y += speed;
            if(glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) cam.target.y -= speed;
        }

        if(glfwGetMouseButton(window,GLFW_MOUSE_BUTTON_MIDDLE)==GLFW_PRESS){
            double x,y; glfwGetCursorPos(window,&x,&y);
            if(!orbiting){lastX=x;lastY=y;orbiting=true;}
            cam.yaw  +=(float)(x-lastX)*0.005f;
            cam.pitch-=(float)(y-lastY)*0.005f;
            cam.pitch = Clamp(cam.pitch, -1.5f, 1.5f);
            lastX=x;lastY=y;
        } else { orbiting=false; }

        {
            Vec3 offset={cosf(cam.pitch)*cosf(cam.yaw),sinf(cam.pitch),cosf(cam.pitch)*sinf(cam.yaw)};
            cam.position=cam.target-Normalize(offset)*cam.distance;
            cam.forward =Normalize(cam.target-cam.position);
        }

        glClearColor(0.53f,0.81f,0.92f,1.0f); // sky blue
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texDiffuse);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,texNormal);

        Mat4 view=cam.GetView();
        Mat4 proj=cam.GetProjection();

        glUseProgram(prog);
        glUniformMatrix4fv(glGetUniformLocation(prog,"uModel"),     1,GL_FALSE,model.m);
        glUniformMatrix4fv(glGetUniformLocation(prog,"uView"),      1,GL_FALSE,view.m);
        glUniformMatrix4fv(glGetUniformLocation(prog,"uProjection"),1,GL_FALSE,proj.m);
        glUniform3f(glGetUniformLocation(prog,"uLightPos"),  lightPos.x,    lightPos.y,    lightPos.z);
        glUniform3f(glGetUniformLocation(prog,"uLightColor"),lightColor.x,  lightColor.y,  lightColor.z);
        glUniform3f(glGetUniformLocation(prog,"uViewPos"),   cam.position.x,cam.position.y,cam.position.z);

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES,0,vertCount);

        glfwSwapBuffers(window);
    }

    glDeleteTextures(1,&texDiffuse);
    glDeleteTextures(1,&texNormal);
    glDeleteVertexArrays(1,&VAO);
    glDeleteBuffers(1,&VBO);
    glDeleteProgram(prog);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}