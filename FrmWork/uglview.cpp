#include "uglview.h"
#include <QFile>
#include <QThread>
#include <QImage>
#include <QMessageBox>
#include <QTextCodec>
#include <QLabel>
#include <QDebug>
#include <QVector3D>
#include <QVector>
#include <math.h>
#include <QHash>
UGLView::UGLView(QWidget *parent) :
    QGLWidget(parent)
{
    openFile="";
    opening=false;
    showFrame=false;
    blend2=false;
    down=false;
    listLen=0;
    lighten=false;
    _dx=0;
    _dy=0;
    _dz=0;
}

void UGLView::initializeGL()
{
    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glClearColor(0,0,0,0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    listBase=glGenLists(2);
}

void UGLView::resizeGL(int wd,int ht)
{
    int px=0,py=0;
    float ps1=1.5*width()/w,ps2=1.5*height()/h;
    psize=(ps1>ps2)?ps2:ps1;
    if(w>0&&h>0)
    {
        float rat=(float)w/h,trat=(float)wd/ht;
        int tmp;
        if(trat>rat)
        {
            tmp=ht*rat;
            px=(wd-tmp)/2;
            wd=tmp;
        }else
        {
            tmp=wd/rat;
            py=(ht-tmp)/2;
            ht=tmp;
        }
    }
    glViewport(px,py,wd,ht);
}
class UOpenThread : public QThread
{
private:
    UGLView* v;
public:
    UOpenThread(UGLView* view)
    {
        v=view;
    }

    void run()
    {
        main();
        v->status="";
        v->opening=false;
    }

    void main()
    {
        QFile f(v->openFile);
        v->opening=false;
        v->status="Reading file";
        if(f.open(f.ReadOnly)){
            v->opening=true;
            QVector<float*> &map=v->map;
            map.clear();

            int &w=v->w,&h=v->h;
            {
                unsigned short _w,_h;
                f.read((char*)&_w,2);
                f.read((char*)&_h,2);
                w=_w;
                h=_h;
            }

            int len=w*h;
            v->dispStr="";
            if(len==0)
            {
                if(f.atEnd())
                {
                    f.close();
                    return;
                }
                f.read(1);
                QTextCodec *cc=QTextCodec::codecForLocale();
                while(!f.atEnd())
                    v->dispStr+=cc->toUnicode(f.readLine())+"\n";
                qDebug()<<v->dispStr;
                f.close();
                return;
            }
            char b;
            Byte min=255,max=10;
            int n=0;
            QVector<Byte*> box;
            float &prog=v->progress;
            while(f.read(&b,1)&&b>0)
            {
                if(prog<0.3)
                    prog+=0.001;
                Byte *img=new Byte[len+10];
                f.read((char*)img,len);
                for(int i=0;i<len;i++)
                {
                    if(img[i]>=10&&img[i]<min)
                    {
                        min=img[i];
                    }else if(img[i]>max)
                    {
                        max=img[i];
                    }
                    n++;
                }
                box.append(img);
            }
            bool mono=(min==max);
            if(mono)
                min=0;
            if(min>50)
                min=50;
            v->status="Analyzing data";
            int c=box.count();

            //make the vertex arrays
            float _w=(float)2/w,
                  _h=(float)2/h,
                  inc=2.0f/box.count();

            v->_dx=_w;
            v->_dy=_h;
            v->_dz=inc;
            int &vSize=v->vertexLastSize;
            vSize=vertexListMax+1;
            float *coord,*color;
            n=0;
            for(int z=0;z<c;z++)
            {
                v->progress=0.3+0.4*z/c;
                Byte* img=box.at(z);
                for(int i=0;i<len;i++)
                {
                    Byte t=img[i];
                    if(t>min)
                    {
                        float col=(float)(t-min)/(max-min);
                        if(col>0.05)
                        {
                            n++;
                            if(vSize>=vertexListMax)
                            {
                                vSize=0;
                                v->vertexCoord.append((coord=new float[3*vertexListMax]));
                                v->vertexColor.append((color=new float[4*vertexListMax]));
                            }
                            const static int SHAKE=1000;
                            float rd=(qrand()%SHAKE/1.0f)/SHAKE;
                            coord[vSize*3]=_h*(i%h+rd/10)-1;
                            coord[vSize*3+1]=_w*(i/h+rd/10)-1;
                            coord[vSize*3+2]=inc*(z+rd)-1;

                            color[vSize*4]=0.1+0.9*col;
                            color[vSize*4+1]=col;
                            color[vSize*4+2]=col;
                            if(!mono)
                                col=col*inc;
                            else
                                col=inc*10;
                            if(col>1)
                                col=1;
                            color[vSize*4+3]=col;
                            vSize++;
                        }
                    }
                }
                delete[] img;
            }
            f.read(&b,1);
            f.close();
        }
    }
};

void UGLView::showUrw(QString path)
{
    openFile=path;
    UOpenThread* t=new UOpenThread(this);
    t->start();
    connect(t,SIGNAL(finished()),SLOT(doneReading()));
    QTimer tm;
    tm.setInterval(100);
    progress=0;
    tm.start();
    connect(&tm,SIGNAL(timeout()),SLOT(updateProg()));
    pdlg.setProgress(0);
    pdlg.exec();
}

void UGLView::updateProg()
{
    if(opening)
    {
        pdlg.setProgress(progress);
        pdlg.setWindowTitle(status);
    }else
    {
        ((QTimer*)sender())->stop();
    }
}

void UGLView::doneReading()
{
    delete sender();
    if(vertexCoord.empty())
    {
        if(!dispStr.isEmpty()){
            emit(setLbl(dispStr));
            hide();
            emit(doneGenerating());
        }
        pdlg.setProgress(2);
    }else{
        emit(setLbl(""));
        show();
        float r1=(float)w/width(),r2=(float)h/height();
        zoom=(r1>r2)?r2:r1;
        zoom=zoom*0.9;
        zzoom=1;
        resizeGL(width(),height());
        updateGL();
    }
}

void UGLView::setLighten(bool b)
{
    lighten=b;
}

inline bool getPoint(QString& s,float& x, float& y,float& z,float *d=0)
{
    QStringList l=s.split(',');
    int c=(d==0)?3:4;
    if(l.count()<c)
        return false;
    bool ok;
    x=l[0].toFloat(&ok);
    if(!ok)
        return false;
    y=l[1].toFloat(&ok);
    if(!ok)
        return false;
    z=l[2].toFloat(&ok);
    if(!ok)
        return false;
    if(d!=0)
    {
        *d=l[3].toFloat(&ok);
        if(!ok)
            *d=0;
    }
    return true;
}

QVector3D sortSource,sortNorm;
bool sortVectorLess(const QVector3D &p1,const QVector3D &p2)
{
    QVector3D t;
    t=p1-sortSource;
    qreal l1=t.length();
    if(l1==0)
        return true;
    qreal a1=t.dotProduct(sortNorm,t)/sortNorm.length()/l1;
    t=p2-sortSource;
    qreal l2=t.length();
    if(l2==0)
        return false;
    qreal a2=t.dotProduct(sortNorm,t)/sortNorm.length()/l2;
    if(a1==a2)
        return l1<l2;
    if(a1>a2)
        return true;
    else
        return false;
}


void UGLView::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    bool raw=false;
    if(!opening&&!vertexCoord.empty())
    {
        generate();
    }else
        raw=view();
    if(blend2)
        glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    else
        glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    glPushMatrix();
    glScalef(zoom,zoom,zoom*zzoom);
    if(showFrame)
    {
        glBegin(GL_LINES);
        glColor4f(1,0,0,0.6);
        glVertex3f(-1,-1,-1);
        glVertex3f(-1,-1,1);
        glColor4f(0,1,0,0.6);
        glVertex3f(-1,-1,-1);
        glVertex3f(-1,1,-1);
        glColor4f(0,0,1,0.6);
        glVertex3f(-1,-1,-1);
        glVertex3f(1,-1,-1);

        glColor4f(1,1,1,0.6);
        glVertex3f(1,-1,1);
        glVertex3f(1,1,1);
        glVertex3f(1,-1,1);
        glVertex3f(1,-1,-1);
        glVertex3f(1,-1,1);
        glVertex3f(-1,-1,1);

        glVertex3f(1,1,-1);
        glVertex3f(1,1,1);
        glVertex3f(1,1,-1);
        glVertex3f(1,-1,-1);
        glVertex3f(1,1,-1);
        glVertex3f(-1,1,-1);

        glVertex3f(-1,1,1);
        glVertex3f(1,1,1);
        glVertex3f(-1,1,1);
        glVertex3f(-1,-1,1);
        glVertex3f(-1,1,1);
        glVertex3f(-1,1,-1);
        glEnd();
    }
    glPointSize(2*(psize*zoom+pzoom));
    foreach(QString item,hints)
    {
        QStringList l=item.split(';');
        bool ok;
        int c=l.count();
        if(c<2)
            continue;
        uint cl=l[0].toUInt(&ok);
        if(!ok)
            continue;
        QColor color(cl);
        glColor4f(color.red()/255.0,color.green()/255.0,color.blue()/255.0,1.0);
        float x=0,y=0,z=0,x2=0,y2=0,z2=0;
        switch(c)
        {
        case 2:
            if(getPoint(l[1],x,y,z,&x2))
            {
                float a=x,b=y,c=z,d=x2;

                y=-d/b*_dy-1;
                z=-d/c*_dz-1;
                QVector<QVector3D> points;
                if(a!=0)
                {
                    y=2/_dy;
                    z=2/_dz;
                    x=-d/a*_dx-1;
                    if(x>-1&&x<1)
                        points.append(QVector3D(x,-1,-1));
                    x=(-d-b*y)/a*_dx-1;
                    if(x>-1&&x<1)
                        points.append(QVector3D(x,1,-1));
                    x=(-d-c*z)/a*_dx-1;
                    if(x>-1&&x<1)
                        points.append(QVector3D(x,-1,1));
                    x=(-d-b*y-c*z)/a*_dx-1;
                    if(x>-1&&x<1)
                        points.append(QVector3D(x,1,1));
                }
                if(b!=0)
                {
                    x=2/_dx;
                    z=2/_dz;
                    y=-d/b*_dy-1;
                    if(y>-1&&y<1)
                        points.append(QVector3D(-1,y,-1));
                    y=(-d-a*x)/b*_dy-1;
                    if(y>-1&&y<1)
                        points.append(QVector3D(1,y,-1));
                    y=(-d-c*z)/b*_dy-1;
                    if(y>-1&&y<1)
                        points.append(QVector3D(-1,y,1));
                    y=(-d-a*x-c*z)/b*_dy-1;
                    if(y>-1&&y<1)
                        points.append(QVector3D(1,y,1));
                }
                if(c!=0)
                {
                    x=2/_dx;
                    y=2/_dy;
                    z=-d/c*_dz-1;
                    if(z>-1&&z<1)
                        points.append(QVector3D(-1,-1,z));
                    z=(-d-a*x)/c*_dz-1;
                    if(z>-1&&z<1)
                        points.append(QVector3D(1,-1,z));
                    z=(-d-b*y)/c*_dz-1;
                    if(z>-1&&z<1)
                        points.append(QVector3D(-1,1,z));
                    z=(-d-a*x-b*y)/c*_dz-1;
                    if(z>-1&&z<1)
                        points.append(QVector3D(1,1,z));
                }

                if(points.empty())
                    continue;

                QVector3D norm,source;
                source=points.at(0);
                norm=points.at(1);
                QVector3D pt;
                int cnt=points.count();
                if(c==0)
                {   //ax+by+cz+d=0
                    if(a==0) //XoZ
                    {
                        if(norm.z()<source.z()||(norm.z()==source.z()&&norm.x()<source.x()))
                        {
                            source=points.at(1);
                            norm=points.at(0);
                        }
                        for(int i=2;i<cnt;i++)
                        {
                            pt=points[i];
                            if(pt.z()<norm.z()||(pt.z()==norm.z()&&pt.x()<norm.x()))
                            {
                                if(pt.z()<source.z()||(pt.z()==source.z()&&pt.x()<source.x()))
                                {
                                    norm=source;
                                    source=pt;
                                }else
                                    norm=pt;
                            }
                        }
                    }else //YoZ
                    {
                        if(norm.z()<source.z()||(norm.z()==source.z()&&norm.y()<source.y()))
                        {
                            source=points.at(1);
                            norm=points.at(0);
                        }
                        for(int i=2;i<cnt;i++)
                        {
                            pt=points[i];
                            if(pt.z()<norm.z()||(pt.z()==norm.z()&&pt.y()<norm.y()))
                            {
                                if(pt.z()<source.z()||(pt.z()==source.z()&&pt.y()<source.y()))
                                {
                                    norm=source;
                                    source=pt;
                                }else
                                    norm=pt;
                            }
                        }
                    }
                }else   //XoY
                {
                    if(norm.y()<source.y()||(norm.y()==source.y()&&norm.x()<source.x()))
                    {
                        source=points.at(1);
                        norm=points.at(0);
                    }
                    for(int i=2;i<cnt;i++)
                    {
                        pt=points[i];
                        if(pt.y()<norm.y()||(pt.y()==norm.y()&&pt.x()<norm.x()))
                        {
                            if(pt.y()<source.y()||(pt.y()==source.y()&&pt.x()<source.x()))
                            {
                                norm=source;
                                source=pt;
                            }else
                                norm=pt;
                        }
                    }
                }
                sortSource=source;
                sortNorm=norm-source;
                qSort(points.begin(),points.end(),sortVectorLess);
                glColor4f(color.red()/255.0,color.green()/255.0,color.blue()/255.0,0.3);
                glBegin(GL_POLYGON);
                foreach(pt,points)
                {
                    glVertex3d(pt.x(),pt.y(),pt.z());
                }
                glEnd();
            }else if(getPoint(l[1],x,y,z))
            {
                glBegin(GL_POINTS);
                glVertex3f(x*_dx-1,y*_dy-1,z*_dz-1);
                glEnd();
            }
            break;
        case 3:
            if(getPoint(l[1],x,y,z)&&getPoint(l[2],x2,y2,z2))
            {
                glBegin(GL_LINES);
                glVertex3f(x*_dx-1,y*_dy-1,z*_dz-1);
                glVertex3f(x2*_dx-1,y2*_dy-1,z2*_dz-1);
                glEnd();
            }
            break;
        }
    }
    glPointSize(psize*zoom+pzoom);

    static uint s=0;
    if(down||raw)
        s=(s+1)%2;
    else
        s=100;

    int n=1;
    if(lighten)
        n+=3;
    while(n--)
        for(uint i=0;i<listLen;i++)
        {
            glCallList(listBase+i);
        }
    glPopMatrix();
}

void UGLView::generate()
{
    static int total=0,cc;
    if(status.isEmpty())
    {
        glLoadIdentity();
        glRotatef(45,1,1,1);
        pdlg.setWindowTitle("Rendering image");
        status="r";
        total=vertexCoord.count();
        if(listLen>0)
        {
            glDeleteLists(listBase,listLen);
            listLen=0;
        }
        cc=total;
    }
    glNewList(listBase+listLen,GL_COMPILE);
    for(int i=0;cc>0&&i<30;i++)
    {
        glVertexPointer(3,GL_FLOAT,0,vertexCoord.last());
        glColorPointer(4,GL_FLOAT,0,vertexColor.last());

        int pc;
        if(cc==1)
        {
            pc=vertexLastSize;
        }else
            pc=vertexListMax;
        glDrawArrays(GL_POINTS,0,pc);
        delete[] vertexCoord.last();
        delete[] vertexColor.last();
        vertexCoord.pop_back();
        vertexColor.pop_back();
        cc--;
    }
    listLen++;
    glEndList();
    if(vertexCoord.empty()){
        emit(doneGenerating());
    }else
        QTimer::singleShot(0,this,SLOT(updateGL()));
    pdlg.setProgress(1.01-0.3*vertexCoord.count()/total);

}

void UGLView::mouseReleaseEvent(QMouseEvent* e)
{
    down=false;
    rotate=false;
    updateGL();
}

void UGLView::mousePressEvent(QMouseEvent *e)
{
    down=true;
    curr=prev=e->pos();
    rotate=(e->button()==Qt::RightButton);
}

void UGLView::mouseMoveEvent(QMouseEvent *e)
{
    if(down){
        curr=e->pos();
        updateGL();
    }
}

void UGLView::wheelEvent(QWheelEvent *e)
{
    int d=e->delta();
    if(e->modifiers()&Qt::ControlModifier)
    {
        if(d>0)
            pzoom+=0.1;
        else
            pzoom-=0.1;
        if(pzoom<-1)
            pzoom=-1;
    }else if(e->modifiers()&Qt::AltModifier)
    {
        if(d>0)
            zzoom+=0.1;
        else
            zzoom-=0.1;
        if(zzoom<0.1)
            zzoom=0.1;
    }else
    {
        if(d>0)
            zoom+=0.1;
        else
            zoom-=0.1;
        if(zoom<0.1)
            zoom=0.1;
    }
    updateGL();
}

#include<math.h>
bool UGLView::view()
{
    if(down&&curr!=prev){
        double alpha;
        QVector3D n;
        if(rotate)
        {
            n=QVector3D(0,0,1);
            QPoint mid(width()/2,height()/2);
            QLineF v1(mid,curr),v2(mid,prev);
            alpha=-v1.angleTo(v2);
        }else
        {
            QPoint l=curr-prev;
            n=QVector3D(l.y(),l.x(),0);
            float r1=(float)l.x()/width(),r2=(float)l.y()/height();
            alpha=sqrt(r1*r1+r2*r2);
            if(alpha<0.1)
                alpha*=100;
            else if(alpha<0.2)
                alpha*=120;
            else
                alpha*=180;
            alpha/=zoom;
        }
        prev=curr;
        double matv[16];
        glGetDoublev(GL_MODELVIEW_MATRIX,matv);
        n=n*QMatrix4x4(matv).inverted();
        glRotated(alpha,n.x(),n.y(),n.z());
        return true;
    }
    return false;
}

void UGLView::setHint(QStringList list)
{
    hints=list;
    updateGL();
}
