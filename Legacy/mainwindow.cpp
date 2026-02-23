#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QThread>
#include <atlstr.h>
#include <stdio.h>
#include "opcda.h"
#include "OPCClient.h"
#include "OPCHost.h"
#include "OPCServer.h"
#include "OPCGroup.h"
#include "OPCItem.h"
#include <sys\timeb.h>
#include <windows.h>
#include "SPIC_Export.h"
#include <math.h>
#include <QMessageBox>
#include "syncserialcomm.h"
#include <QFile>
#include <QString>
#include <vector>
#include <QChar>
#include <QMetaEnum>
#include <iostream>
//#include "gcparser.h"
//#include <QDebug>
//extern unsigned short char_counter = 0;
extern QChar gc_letter=NULL;
extern QString Instruction_letter=NULL;
extern QString gc_value=NULL;
float Layer_number=0;
float Layer_Thickness=0;
int isLayerAsked=0;
//LineDecoder Decode;
QChar L_letter;
bool portOpend=0;
QString Galvo_Says;

//-------------------------------------//
SyncSerialComm sport("COM6");//("\\\\.\\COM4");
    DWORD dwSize=1;
    DWORD dwFlag;
    QString Galvo_Read="L2H3500\0";
    unsigned short TotalLayers = 100;// My code Starts here....
    unsigned short currentLayer = 0;
    HRESULT hResult;
    //SyncSerialComm sport("COM6");//("\\\\.\\COM4");

    char *Read_From_Galvo=new char[10];// NULL;//new char[5];//"a";
    const char *Write_to_Galvo=NULL;
    const char *EndInstruction="E";
   // const char *Islayerasked="Layer";
    char Temp[20];

 //sprintf(Temp,"Fq");
 //Write_to_Galvo=Temp;
    int kk=0;
//-------------------------------------//
extern int PreStakesLeft=0;
extern int PreSurfaceStatus=1;
bool Pre_varPowderSurfaceDone=0;

extern int My_Flag=1;
VARIANT var;  //
int varSourceCylPosition,varSinkCylPosition,varStacksLeft,varSurfaceStatus, varStartUpStatus ;//
extern int CurrtStacksLeft =2;
COPCItem_DataMap opcData;
 //gcParser parse;
/**
* Handle asynch data coming from changes in the OPC group
*/
/*class CMyCallback:public IAsynchDataCallback{
    public:
    CMyCallback::CMyCallback(MainWindow* owner): IAsynchDataCallback()
    {mOwner=owner;}


    void OnDataChange(COPCGroup & group, CAtlMap<COPCItem *, OPCItemData *> & changes){
        printf("Group %s, item changes\n", group.getName());

        POSITION pos = changes.GetStartPosition();
        while (pos != NULL){
            COPCItem * item = changes.GetKeyAt(pos);
            OPCItemData* val= changes.GetValueAt(pos);//  .GetValueAt(pos); // GetNextValue(pos);//  GetValueAt(pos);
            printf("-----> %s %i \n", item->getName(), val->vDataValue.intVal);
            item = changes.GetNextKey(pos);

//            mOwner->dataChanged(item,val);
        }
    }

    private:
    MainWindow* mOwner=nullptr;
};*/
class CMyCallback:public IAsynchDataCallback{
    public:
    CMyCallback::CMyCallback(MainWindow* owner): IAsynchDataCallback()
    {mOwner=owner;}


    void OnDataChange(COPCGroup & group, CAtlMap<COPCItem *, OPCItemData *> & changes){
        printf("Group %s, item changes\n", group.getName());

        POSITION pos = changes.GetStartPosition();
        while (pos != NULL){
            COPCItem * item = changes.GetKeyAt(pos);
            OPCItemData* val= changes.GetValueAt(pos);//  .GetValueAt(pos); // GetNextValue(pos);//  GetValueAt(pos);
            printf("-----> %s %i \n", item->getName(), val->vDataValue.intVal);
            item = changes.GetNextKey(pos);

//            mOwner->dataChanged(item,val);
        }
    }

    private:
    MainWindow* mOwner=nullptr;
};



/**
*	Handle completion of asynch operation
*/
/*class CTransComplete:public ITransactionComplete{
    CString completionMessage;
public:
    CTransComplete(){
        completionMessage = "Asynch operation is completed";
    }

    void complete(CTransaction &transaction){
        printf("%s\n",completionMessage);
    }

    void setCompletionMessage(const CString & message){
        completionMessage = message;
    }
};*/
class CTransComplete:public ITransactionComplete{
    CString completionMessage;
public:
    CTransComplete(){
        completionMessage = "Asynch operation is completed";
    }

    void complete(CTransaction &transaction){
        printf("%s\n",completionMessage);
    }

    void setCompletionMessage(const CString & message){
        completionMessage = message;
    }
};

/*void checkMsg()
{
    MSG msg;
    while(PeekMessage(&msg,NULL,NULL,NULL,PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        Sleep(1);
    }
}*/
void checkMsg()
{
    MSG msg;
    while(PeekMessage(&msg,NULL,NULL,NULL,PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        Sleep(1);
    }
}

#define MESSAGEPUMPUNTIL(x)	while(!x){{MSG msg;while(PeekMessage(&msg,NULL,NULL,NULL,PM_REMOVE)){TranslateMessage(&msg);DispatchMessage(&msg);Sleep(1);}Sleep(1);}}


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{

    ui->setupUi(this);

    CTransComplete  completion_handler;
    CMyCallback     asynchRead_CallBack(this);


    /*************************************
     * Detect & initialize OPC server
     * Yapılabilecekler:
     * 1-Sonuçlar ana pencerede log penceresine yazdırılabilir
     * 2-Arka planda belirli aralıklarla sunucu bağlantısı kontrol edilip
     * ana pencerede gösterilebilir.
     * 3-Sunucu ismi program değişkeni olabilir.
     *************************************/
    COPCClient::init();
 //  Commented by  Shahidjuly 7
    CString hostName ="Shahid";//"localhost";//"Shahid"; //"localhost" olabilir mi??? ibm
    COPCHost *host = COPCClient::makeHost(hostName);
    printf("*** TAnildi %ld\n");
    //List servers
    CAtlArray<CString> localServerList;
    host->getListOfDAServers(IID_CATID_OPCDAServer20, localServerList);
    unsigned i = 0;
    for (; i < localServerList.GetCount(); i++){
       printf("%s\n", localServerList[i]);
    }

    // connect to OPC
    CString serverName = "CoDeSys.OPC.DA";
    COPCServer *opcServer = host->connectDAServer(serverName);

    // Check status
    ServerStatus status;
    opcServer->getStatus(status);
    printf("*** Server state is %ld\n", status.dwServerState);

    // browse server
    printf("*** Server Item Names: \n");
    CAtlArray<CString> opcItemNames;
    opcServer->getItemNames(opcItemNames);
    printf("Got %d names\n", opcItemNames.GetCount());
    for (i = 0; i < opcItemNames.GetCount(); i++){
        printf("%s\n",opcItemNames[i]);
    }  // By shahid Aug 12

    /*************************************
     * gruplar oluştur.
     * Kolay yöntem:
     * ihtiyaca göre bir kaç değişik grup oluşturmak
     * 1- Arada bir belirli zamanlarda okunacak değişkenler; mGroup_PeriodicRead
     * 2- Değişiklikleri sürekli takip edilmesi gereken değişkenler; mGroup_AsynchRead
     * 3- Kullanıcı emirlerinden oluşan grup; mGroup_Commands
     *
     *************************************/
    //*****************************
    // Between line 143 to 183 is disabled by shahid..
    //************************************************************/
    unsigned long refreshRate; //Şimdilik kullanılmıyor. Server istenen yenileme hızını sağlayamayabilir.
    // mGroup_PeriodicRead = opcServer->makeGroup("group_PeriodicRead",true,1000,refreshRate,0.0);
    // mGroup_Commands = opcServer->makeGroup("group_Commands",true,1000,refreshRate,0.0);

    // setup start group
    //disabled  between 242 and 310  by shahid on 12 Aug 2017
    mGroup_Default=opcServer->makeGroup("group_Default",true,1000,refreshRate,0.0);

    mName_StartUp.SetString("CECC.MaTe_DLMS.StartUpSequence.StartUp");
    mItem_StartUp= mGroup_Default->addItem(mName_StartUp, true); // problem ın this line..

    //connect(ui->btnStartUp,SIGNAL(pressed()),this,SLOT(startUpPressed()));
    // setup asynch readback group (sorun çıktı, şimdilik timer a bağlı olarak "synchread" yapıyor)
    mGroup_AsynchRead = opcServer->makeGroup("group_AsynchRead",true,200,refreshRate,0.0);
    CAtlArray<HRESULT>      errors;
    //printf("\n *** Asynch Readback Group <- add cylinder positions and layers left \n");
    //--------------------------------------------MakeSurface---------------------------//
    mNames_readback.Add("CECC.MaTe_DLMS.MakeSurface.Marcer_Source_Cylinder_ActualPosition");		// 0
    mNames_readback.Add("CECC.MaTe_DLMS.MakeSurface.Marcer_Sink_Cylinder_ActualPosition");		// 1
    mNames_readback.Add("CECC.MaTe_DLMS.MakeSurface.Z_Stacks");									// 2
    mNames_readback.Add("CECC.MaTe_DLMS.MakeSurface.MakeSurface_Done");							// 3
    //--------------------------------------------StartUpSequence---------------------------//
    mNames_readback.Add("CECC.MaTe_DLMS.StartUpSequence.StartUp_Done");							// 4
    //--------------------------------------------GVL---------------------------------------//
    mNames_readback.Add("CECC.MaTe_DLMS.GVL.g_Marcer_Source_Cylinder_ActualPosition");	        // 5
    mNames_readback.Add("CECC.MaTe_DLMS.GVL.g_Marcer_Sink_Cylinder_ActualPosition");		        // 6
    //--------------------------------------------Prepare2Process---------------------------//
    mNames_readback.Add("CECC.MaTe_DLMS.Prepare2Process.LaySurface_Done");						// 7

    if (mGroup_AsynchRead->addItems(mNames_readback, mItems_readback,errors,true) != 0){
        printf("  Asynch Readback Group : Item create failed\n");
    }


    // setup layers group
    //--------------------------------------------MakeSurface  ---refill ---------------------------//
    mGroup_Layers=opcServer->makeGroup("group_Layers",true,1000,refreshRate,0.0);
    printf("\n *** Layers Group <- add cylinder feed amounts and total stack count\n");
    mName_layersMax.SetString("CECC.MaTe_DLMS.MakeSurface.Z_Stacks");printf("\n *** Layers Group <- 0\n");
    mItem_layersMax= mGroup_Layers->addItem(mName_layersMax, true);printf("\n *** Layers Group <- 1- %p\n",mItem_layersMax);
    mName_delta_Source.SetString("CECC.MaTe_DLMS.MakeSurface.Delta_Source");
    mItem_delta_Source= mGroup_Layers->addItem(mName_delta_Source, true);printf("\n *** Layers Group <- 2\n");
    mName_delta_Sink.SetString("CECC.MaTe_DLMS.MakeSurface.Delta_Sink");
    mItem_delta_Sink= mGroup_Layers->addItem(mName_delta_Sink, true);printf("\n *** Layers Group <- 3\n");
    mName_MakeSurface_Done.SetString("CECC.MaTe_DLMS.MakeSurface.MakeSurface_Done");
    mItem_MakeSurface_Done= mGroup_Layers->addItem(mName_MakeSurface_Done, true);printf("\n *** Layers Group <- 4\n");
    mName_Marcer_Source_Cylinder_ActualPosition.SetString("CECC.MaTe_DLMS.MakeSurface.Marcer_Source_Cylinder_ActualPosition");
    mItem_Marcer_Source_Cylinder_ActualPosition= mGroup_Layers->addItem(mName_Marcer_Source_Cylinder_ActualPosition, true);printf("\n *** Layers Group <- 5\n");
    mName_Marcer_Sink_Cylinder_ActualPosition.SetString("CECC.MaTe_DLMS.MakeSurface.Marcer_Sink_Cylinder_ActualPosition");
    mItem_Marcer_Sink_Cylinder_ActualPosition= mGroup_Layers->addItem(mName_Marcer_Sink_Cylinder_ActualPosition, true);printf("\n *** Layers Group <- 6\n");
   //
    mName_StartSurfaces.SetString("CECC.MaTe_DLMS.GVL.StartSurfaces");
    mItem_StartSurfaces= mGroup_Layers->addItem(mName_StartSurfaces, true);//printf("\n *** Layers Group <- 7\n");
     //--------------------------------------------GVL---------------------------//
    mName_g_Marcer_Source_Cylinder_ActualPosition.SetString("CECC.MaTe_DLMS.GVL.g_Marcer_Source_Cylinder_ActualPosition");
    mItem_g_Marcer_Source_Cylinder_ActualPosition= mGroup_Layers->addItem(mName_g_Marcer_Source_Cylinder_ActualPosition, true);printf("\n *** Layers Group <- 8\n");
    mName_g_Marcer_Sink_Cylinder_ActualPosition.SetString("CECC.MaTe_DLMS.GVL.g_Marcer_Sink_Cylinder_ActualPosition");
    mItem_g_Marcer_Sink_Cylinder_ActualPosition= mGroup_Layers->addItem(mName_g_Marcer_Sink_Cylinder_ActualPosition, true);printf("\n *** Layers Group <- 9\n");
    //--------------------------------------------Prepare2Process  for creating layers before lasing---------------------------//
    mName_LaySurface.SetString("CECC.MaTe_DLMS.Prepare2Process.LaySurface");//1
    mItem_LaySurface= mGroup_Layers->addItem(mName_LaySurface, true);printf("\n *** Layers Group <- 10\n");//2
    mName_LaySurface_Done.SetString("CECC.MaTe_DLMS.Prepare2Process.LaySurface_Done");//1
    mItem_LaySurface_Done= mGroup_Layers->addItem(mName_LaySurface_Done, true);printf("\n *** Layers Group <- 11\n");//2
    mName_Step_Sink.SetString("CECC.MaTe_DLMS.Prepare2Process.Step_Sink");//1
    mItem_Step_Sink= mGroup_Layers->addItem(mName_Step_Sink, true);printf("\n *** Layers Group <- 12\n");//2
    mName_Step_Source.SetString("CECC.MaTe_DLMS.Prepare2Process.Step_Source");//1
    mItem_Step_Source= mGroup_Layers->addItem(mName_Step_Source, true);printf("\n *** Layers Group <- 13\n");//2
    mName_Lay_Stacks.SetString("CECC.MaTe_DLMS.Prepare2Process.Lay_Stacks");//1
    mItem_Lay_Stacks= mGroup_Layers->addItem(mName_Lay_Stacks, true);printf("\n *** Layers Group <- 14\n");//2
    //--------------------------------------------StartUpSequence---------------------------//
    mName_StartUp.SetString("CECC.MaTe_DLMS.StartUpSequence.StartUp");//1
    mItem_StartUp= mGroup_Layers->addItem(mName_StartUp, true);printf("\n *** Layers Group <- 15\n");//2
    mName_StartUp_Done.SetString("CECC.MaTe_DLMS.StartUpSequence.StartUp_Done");//1
    mItem_StartUp_Done= mGroup_Layers->addItem(mName_StartUp_Done, true);printf("\n *** Layers Group <- 16\n");
    //---------------------------------------------------------------------------//
   // connect(ui->btnStartLayers,SIGNAL(pressed()),this,SLOT(startLayersPressed()));
     //varPowderSurfaceDone=0;


    //printf("\n *** Starting timer\n");
    mCallbackTimer =startTimer(100);
    //---------------------------------
       ui->textEdit->clear();
    hResult=sport.Open(); // opens the com port
    if(hResult==S_OK)
    {ui->textEdit->append(QString("Port Opened! "));
    hResult=sport.ConfigPort(CBR_19200,5);
    if(hResult==S_OK)
    {ui->textEdit->append(QString("Port Configured! "));
     portOpend=1;}}
    //qDebug() << "Port Configured ";
}

MainWindow::~MainWindow()
{
    delete ui;
}


void MainWindow::timerEvent(QTimerEvent *event)
{
    if(event->timerId()==mCallbackTimer)
    {  if(!portOpend)
            return;  //  if port not connected do nothing here....and return
        // qDebug()<<"\n I am ISR: ";
        // TUGRUL: save stacksLeft to prevstacksLeft
        // ---- disabled by shahid 12 Aug 2017-------//
       Pre_varPowderSurfaceDone=  varPowderSurfaceDone;
        // Pre_LaySurface_Done = varLaySurface_Done
        PreSurfaceStatus=varSurfaceStatus;
        // SYNCH read on Group
        COPCItem_DataMap opcData;
        mGroup_AsynchRead->readSync(mItems_readback,opcData, OPC_DS_DEVICE);
        POSITION pos=opcData.GetStartPosition();

        //repeat for all [key,data] pairs
        int count=opcData.GetCount();
        COPCItem *key=opcData.GetKeyAt(pos);
        OPCItemData *data=opcData.GetNextValue(pos);
        while(count)
        {
            CString itemname=key->getName();
            if(itemname==mNames_readback.GetAt(0))
            {
                // CECC.MaTe.MakeSurface.Marcer_Source_Cylinder_ActualPosition
                ui->sourceCylPos->display(data->vDataValue.intVal);
                varSourceCylPosition=data->vDataValue.intVal;
            }
            else if(itemname==mNames_readback.GetAt(1))
            {
                // CECC.MaTe.MakeSurface.Marcer_Sink_Cylinder_ActualPosition
                ui->sinkCylPos->display(data->vDataValue.intVal);
                varSinkCylPosition=data->vDataValue.intVal;
            }
            else if(itemname==mNames_readback.GetAt(2))
            {
                // CECC.MaTe.MakeSurface.Z_Stacks
                ui->stacksLeft->display(data->vDataValue.intVal);
                varStacksLeft=data->vDataValue.intVal;
            }
            else if(itemname==mNames_readback.GetAt(3))
            {
                // CECC.MaTe.MakeSurface.MakeSurface_Done
                varReady2Powder=data->vDataValue.intVal;
                ui->Ready2Powder->display(data->vDataValue.intVal);

            }
            else if(itemname==mNames_readback.GetAt(4))
            {
                // CECC.MaTe.StartUpSequence.StartUp_Done
                ui->StartUpDone->display(data->vDataValue.intVal);
                varStartUpDone=data->vDataValue.intVal;

            }
            else if(itemname==mNames_readback.GetAt(5))
            {
                // CECC.MaTe.MakeSurface.g_Marcer_Source_Cylinder_ActualPosition
                ui->g_sourceCylPos->display(data->vDataValue.intVal);
                varg_sourceCylPos=data->vDataValue.intVal;
            }
            else if(itemname==mNames_readback.GetAt(6))
            {
                // CECC.MaTe.MakeSurface.g_Marcer_Sink_Cylinder_ActualPosition
                ui->g_sinkCylPos->display(data->vDataValue.intVal);
                varg_sinkCylPos=data->vDataValue.intVal;
            }
            else if(itemname==mNames_readback.GetAt(7))
            {
                // CECC.MaTe.Prepare2Process.LaySurface_Done
                ui->PowderSurfaceDone_2->display(data->vDataValue.intVal);
                varPowderSurfaceDone=data->vDataValue.intVal;

            }
            //---------------------------------------------------//

            count--;
            if(count)
            {
                key= opcData.GetKeyAt(pos);
                data= opcData.GetNextValue(pos);
            }
        }
//----------------------------------Write Code in this loop-----------------------//
          //------------write to Galvo In case varPowderSurfaceDone----------//
    // qDebug()<<"\n powder surface :"<<  varPowderSurfaceDone;


      //  this line isdisabled by shahid
     //if(Pre_varPowderSurfaceDone==false && varPowderSurfaceDone==true)// && My_Flag!=0)//PreStakesLeft==1 && varStacksLeft==0 &   varSurfaceStatus==0)


   if(Pre_varPowderSurfaceDone==false && varPowderSurfaceDone==true)// && My_Flag!=0)//PreStakesLeft==1 && varStacksLeft==0 &   varSurfaceStatus==0)

    {   //qDebug()<<"\n  Layers Built:";
        //varPowderSurfaceDone=0;
         ui->textEdit->append(QString("Layer Prepared! "));
         Pre_varPowderSurfaceDone=0;
         My_Flag==0;
         sprintf(Temp,"OK#");
         Write_to_Galvo=Temp;
         dwSize=strlen(Write_to_Galvo);
        try { hResult= sport.Write(Write_to_Galvo,  dwSize); dwFlag =  PURGE_TXCLEAR;
        //Flush( dwFlag = PURGE_TXCLEAR | PURGE_RXCLEAR);
             // sport.Flush(dwFlag);
              if (hResult == E_FAIL) // opens the com port
                    throw 0; }
              catch (int e)
                { ::MessageBox(0, "Data Sending to Galvo  Failed.", 0, MB_OK);}

   ui->textEdit->append(QString("Laser Now! "));
   }
                    // ---------to Galvo ends------------//

  /////-------------------------------Read from Galvo--------------////
             //strcpy(Read_From_Galvo ,1 ,"F");
              sprintf(Temp,"F");
             Read_From_Galvo=Temp;
            try {dwSize=0;
            // qDebug()<<"\n Waiting for galvo Galvo :";
            hResult= sport.Read(&Read_From_Galvo,dwSize);
            if (hResult == E_FAIL) // opens the com port
                throw 0;
                else
            {    //Galvo_Read= 	QString(Read_From_Galvo);//QString::fromLocal8Bit(Read_From_Galvo);//(Problem is with readfloat or conversion to Qt string ?)

    //----------------------------Checking weather to stop?--------------------------------------//
                 //if(strcmp(Read_From_Galvo,EndInstruction)==0)
                 //{   delete [] Read_From_Galvo; qDebug()<<"\n Galvo Says End :";
                  ////killTimer(mCallbackTimer);
                 //}  //  breaks loop if Galvo Says END..

            dwFlag =  PURGE_RXCLEAR;
            //ush( dwFlag = PURGE_TXCLEAR | PURGE_RXCLEAR);
            sport.Flush(dwFlag);
                    }
            }

        catch (int e)
                 {::MessageBox(0, "Servo Data  Recieve  Failed.", 0, MB_OK);}
                //  Added   by shahid  for testing
           //QString line1=QString("A0")+QString(Read_From_Galvo);
           //qDebug()<< line1;


          //if(strcmp(Read_From_Galvo,Islayerasked)==0)
           Galvo_Says.clear();
           Galvo_Says=QString(Read_From_Galvo);
          //  process termination processing...//
           if(Galvo_Says.contains("END",Qt::CaseInsensitive))
           { ui->textEdit->append(QString("Process Ended! "));
               //mCallbackTimer =startTimer(100);
               killTimer(mCallbackTimer);
           }

          //



        /*if(Galvo_Says.contains("L",Qt::CaseInsensitive))
          {
          Galvo_Says.remove(0,1);
          //float Layer_number=Galvo_Says.toFloat();
          ui->textEdit->append(QString("Processing Layer Number:= ")+QString(Galvo_Says));
           }*/

           /*if(isLayerAsked)
            {    isLayerAsked=0;
                 //Read_From_Galvo=NULL;
                 Pre_varPowderSurfaceDone=1;
                 qDebug()<<"\n Thickness :"<< Layer_Thickness;


           }*/
               //  added ends here
               ////  From Galvo Read Ends...........////////
   //  Disabled By shahid 12 Aug 2017
    if(Galvo_Says.contains("L",Qt::CaseInsensitive)) //(strcmp(Read_From_Galvo,Islayerasked)==0)
    {
      //Galvo_Says.remove(0,1);
      ui->textEdit->append(QString("Galvo requested:  ")+ Galvo_Says);
      //Layer_Thickness=Galvo_Says.toFloat();  //  conver to int
      //qDebug()<<"\n Thickness :"<< Layer_Thickness;
    Sleep(10);   //PLC access ends here
    //qDebug()<<"\n Writing to PLC :";
    //VARIANT var;  //  PLC  Access Startrs Here......
    var.vt = VT_INT;
    var.iVal =1;// round(ui->noOfStacks->value());// this has to be read through serial
    mItem_Lay_Stacks->writeSync(var);
    Sleep(100);
    var.vt = VT_INT;
    var.iVal =round(ui->deltaSource->value()); // int(Layer_Thickness);this has to be read through serial

    mItem_Step_Source->writeSync(var);
    Sleep(100);
    var.vt = VT_INT;
    var.iVal =round(ui->deltaSink->value());//int(Layer_Thickness);
    mItem_Step_Sink->writeSync(var); //  read using serial..
    Sleep(100);
     // var.vt = VT_BOOL;
    //var.iVal = TRUE;
    //mItem_startLayers->writeSync(var);
    //Sleep(400);  //PLC access ends here
    var.vt = VT_BOOL;
    var.iVal = TRUE;
    mItem_LaySurface->writeSync(var); // mItem_Lay_Stacks=​ Number of surfaces (powder spreads) prepared before the lasing action.​
    Sleep(400);
    } // ends layer cond


//----------------------------------End By Shahid---------------------------------
        //TUGRUL:
        //if "stacksLeft==0 and prevstacksLeft==1" call method "sendCommandToGalvo()";
        //else call method "receiveCommandFromGalvo()";
        //END TUGRUL

    }
    else
        QObject::timerEvent(event);
}
//TUGRUL: 2 methods defined and called from timer
//method sendCommandToGalvo() {send start command to Galvo}
//method receiveCommandFromGalvo() {try receiving from serial port; if command received and stacksLeft==0 call startLayersPressed() with value=1;}
//END TUGRUL:





void MainWindow::on_Prep_Powder_Fill_clicked()
{  // Disabled by shahid 12 Aug 17
    Sleep(100);   //PLC access ends here
    //VARIANT var;  //  PLC  Access Startrs Here......
    var.vt = VT_INT;
    var.iVal = round(ui->noOfStacks->value());// this has to be read through serial
    mItem_layersMax->writeSync(var);
    mItem_Lay_Stacks->writeSync(var);
    Sleep(100);
    var.vt = VT_INT;
    var.iVal = round(ui->deltaSource->value()); // this has to be read through serial
    mItem_delta_Source->writeSync(var);
    Sleep(100);
    var.vt = VT_INT;
    var.iVal = round(ui->deltaSink->value());
    mItem_delta_Sink->writeSync(var); //  read using serial..
    Sleep(100);
    var.vt = VT_BOOL;
    var.iVal = TRUE;

   mItem_StartSurfaces->writeSync(var); //=​ Prepares the system for Powder Refill​

   // varPowderSurfaceDone;
  // mItem_Lay_Stacks->writeSync(var);
     //qDebug()<<"\n powder surface :"<<  varPowderSurfaceDone;

   Sleep(500); //PLC access ends here

}

void MainWindow::on_Lay_Surface_clicked()
{
    // Disabled By Shahid on 12 August 2017
     /* Sleep(100);   //PLC access ends here
        qDebug()<<"\n Writing to PLC :";
        //VARIANT var;  //  PLC  Access Startrs Here......
        var.vt = VT_INT;
        var.iVal =1;// round(ui->noOfStacks->value());// this has to be read through serial
        mItem_Lay_Stacks->writeSync(var);
        Sleep(100);
        var.vt = VT_INT;
        var.iVal =4000;//round(ui->deltaSource->value()); // this has to be read through serial
        mItem_Step_Source->writeSync(var);
        Sleep(100);
        var.vt = VT_INT;
        var.iVal = 4000;//round(ui->deltaSink->value());
        mItem_Step_Sink->writeSync(var); //  read using serial..
        Sleep(100);
       // var.vt = VT_BOOL;
        //var.iVal = TRUE;
       // mItem_startLayers->writeSync(var);
        //Sleep(400);  //PLC access ends here
        var.vt = VT_BOOL;
        var.iVal = TRUE;
       mItem_LaySurface->writeSync(var); // mItem_Lay_Stacks=​ Number of surfaces (powder spreads) prepared before the lasing action.​
        Sleep(400);*/


}



void MainWindow::on_StartUP_clicked()
{
    var.vt = VT_BOOL;
   var.iVal = TRUE;
    //mItem_startUp->writeSync(var);
     mItem_StartUp->writeSync(var);
    //mItem_StartUp_Done;
}

void MainWindow::on_MakeBottomLayers_clicked()
{       // Disabled By Shahid on 12 Aug 2017
        //Sleep(500);   //PLC access ends here
        //qDebug()<<"\n Writing to PLC :";
        //VARIANT var;  //  PLC  Access Startrs Here......
        var.vt = VT_INT;
        var.iVal =round(ui->noOfStacks_BottomLayer->value());// this has to be read through serial
        mItem_Lay_Stacks->writeSync(var);
        Sleep(1000);
        var.vt = VT_INT;
        var.iVal =round(ui->deltaSource_BottomLayer->value()); // this has to be read through serial
        mItem_Step_Source->writeSync(var);
        Sleep(1000);
        var.vt = VT_INT;
        var.iVal = round(ui->deltaSink_BottomLayer->value());
        mItem_Step_Sink->writeSync(var); //  read using serial..
        Sleep(1000);
       // var.vt = VT_BOOL;
        //var.iVal = TRUE;
        //mItem_startLayers->writeSync(var);
       // Sleep(400);  //PLC access ends here
        var.vt = VT_BOOL;
        var.iVal = TRUE;
       mItem_LaySurface->writeSync(var); // mItem_Lay_Stacks=​ Number of surfaces (powder spreads) prepared before the lasing action.​
        Sleep(500);



}

void MainWindow::on_Restart_process_clicked()
{//COPCClient::init();
     mCallbackTimer =startTimer(100);

}
