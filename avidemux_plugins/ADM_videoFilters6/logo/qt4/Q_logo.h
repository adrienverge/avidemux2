#ifndef Q_mpdelogo_h
#define Q_mpdelogo_h

#include "ui_logo.h"
#include "DIA_flyDialog.h"
#include "ADM_image.h"
#include "DIA_flyDialogQt4.h"
#include "DIA_flyLogo.h"
#include "QMouseEvent"
/**
 * 
 * @return 
 */
class  ADM_LogoCanvas : public ADM_QCanvas
{
    Q_OBJECT
protected:
public:
	
                ADM_LogoCanvas(QWidget *z, uint32_t w, uint32_t h);
	virtual ~ADM_LogoCanvas();
        void mousePressEvent(QMouseEvent * event);
        void mouseReleaseEvent(QMouseEvent * event);
        void moveEvent(QMoveEvent * event);        

signals:
        void movedSignal(int newx, int newy);                
};

/**
 * 
 * @return 
 */
class Ui_logoWindow : public QDialog
{
	Q_OBJECT

protected: 
	int lock;
       

public:
	
                            Ui_logoWindow(QWidget *parent, logo *param, ADM_coreVideoFilter *in);
                            ~Ui_logoWindow();
	Ui_mpdelogoDialog   ui;
        ADM_coreVideoFilter *_in;
        flyLogo             *myCrop;
	ADM_LogoCanvas      *canvas;
public slots:
	void                gather(logo *param);

private slots:
	void                sliderUpdate(int foo);
	void                valueChanged(int foo);
        void                moved(int x,int y);
        void                preview(int x);
       
    
};
#endif