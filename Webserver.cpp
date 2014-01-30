/****************************************************************************************************

RepRapFirmware - Webserver

This class serves a single-page web applications to the attached network.  This page forms the user's 
interface with the RepRap machine.  This software interprests returned values from the page and uses it
to Generate G Codes, which it sends to the RepRap.  It also collects values from the RepRap like 
temperature and uses those to construct the web page.

The page itself - reprap.htm - uses Knockout.js and Jquery.js.  See:

http://knockoutjs.com/

http://jquery.com/

-----------------------------------------------------------------------------------------------------

Version 0.2

10 May 2013

Adrian Bowyer
RepRap Professional Ltd
http://reprappro.com

Licence: GPL

****************************************************************************************************/

#include "RepRapFirmware.h"

//***************************************************************************************************


bool Webserver::MatchBoundary(char c)
{
  if(!postBoundary[0])
    return false;
    
  if(c == postBoundary[boundaryCount])
  {
    boundaryCount++;
    if(!postBoundary[boundaryCount])
    {
      boundaryCount = 0;
      return true;
    }
  } else
  {
    for(int i = 0; i < boundaryCount; i++)
      postFile->Write(postBoundary[i]);
    postFile->Write(c);
    boundaryCount = 0;
  }
  return false;  
}


//****************************************************************************************************

// Feeding G Codes to the GCodes class

bool Webserver::GCodeAvailable()
{
  return gcodeReadIndex != gcodeWriteIndex;
}

byte Webserver::ReadGCode()
{
  byte c;
  if (gcodeReadIndex == gcodeWriteIndex)
  {
	  c = 0;
  }
  else
  {
	  c = gcodeBuffer[gcodeReadIndex];
	  gcodeReadIndex = (gcodeReadIndex + 1u) % gcodeBufLength;
  }
  return c;
}

// Process a received string of gcodes
bool Webserver::LoadGcodeBuffer(const char* gc, bool convertWeb)
{
	char gcodeTempBuf[GCODE_LENGTH];
	uint16_t gtp = 0;
	char c;
	do
	{
		c = *gc++;
		if(c == 0)
		{
			gcodeTempBuf[gtp] = 0;
			ProcessGcode(gcodeTempBuf);
			gtp = 0;
		}
		else if(gtp == ARRAY_SIZE(gcodeTempBuf) - 1)
		{
			// gcode is too long, we haven't room for another character and a null
		    platform->Message(HOST_MESSAGE, "Webserver: GCode local buffer overflow.\n");
		    HandleReply("Webserver: GCode local buffer overflow", true);
		    return false;
		}
		else if(c == '+' && convertWeb)
	    {
			gcodeTempBuf[gtp++] = ' ';
	    }
	    else if(c == '%' && convertWeb)
	    {
	    	c = *gc++;
	    	if (c != 0)
	    	{
	    		unsigned char uc;
	    		if(isalpha(c))
	    		{
	    			uc = 16*(c - 'A' + 10);
	    		}
	    		else
	    		{
	    			uc = 16*(c - '0');
	    		}
	    		c = *gc++;
	    		if (c != 0)
	    		{
	    			if(isalpha(c))
	    			{
	    				uc += c - 'A' + 10;
	    			}
	    			else
	    			{
	    				uc += c - '0';
	    			}
	    			if (uc == '\n')
	    			{
	    				gcodeTempBuf[gtp] = 0;
	    				ProcessGcode(gcodeTempBuf);
	    				gtp = 0;
	    			}
	    			else
	    			{
	    				gcodeTempBuf[gtp++] = uc;
	    			}
	    		}
	    	}
	    }
	    else
	    {
	    	gcodeTempBuf[gtp++] = c;
	    }
	}
	while (c != 0);
	return true;
}

// Process a null-terminated gcode
// We intercept four G/M Codes so we can deal with file manipulation and emergencies.  That
// way things don't get out of sync, and - as a file name can contain
// a valid G code (!) - confusion is avoided.

void Webserver::ProcessGcode(const char* gc)
{
  int8_t specialAction = 0;
  if(StringStartsWith(gc, "M30 "))
  {
	  specialAction = 1;
  }
  else if(StringStartsWith(gc, "M23 "))
  {
	  specialAction = 2;
  }
  else if(StringStartsWith(gc, "M112") && !isdigit(gc[4]))
  {
	  specialAction = 3;
  }
  else if(StringStartsWith(gc, "M503") && !isdigit(gc[4]))
  {
	  specialAction = 4;
  }
  
  if(specialAction != 0) // Delete or print a file?
  { 
    switch (specialAction)
    {
    case 1: // Delete
      reprap.GetGCodes()->DeleteFile(&gc[4]);
      break;

    case 2:	// print
      reprap.GetGCodes()->QueueFileToPrint(&gc[4]);
      break;

    case 3:
      reprap.EmergencyStop();
      break;

    case 4:
	  {
		FileStore *configFile = platform->GetFileStore(platform->GetSysDir(), platform->GetConfigFile(), false);
		if(configFile == NULL)
		{
		  HandleReply("Configuration file not found", true);
		}
		else
		{
		  char c;
		  size_t i = 0;
		  while(i < STRING_LENGTH && configFile->Read(c))
		  {
			gcodeReply[i++] = c;
		  }
		  configFile->Close();
		  gcodeReply[i] = 0;
		  ++seq;
		}
	  }
	  break;
    }
  }
  else if(strlen(gc) + 1 > GetGcodeBufferSpace())
  {
	  platform->Message(HOST_MESSAGE, "Webserver: GCode buffer overflow.\n");
	  HandleReply("Webserver: GCode buffer overflow", true);
  }
  else
  {
	  char c;
	  do
	  {
		  c = *gc++;
		gcodeBuffer[gcodeWriteIndex] = c;
		gcodeWriteIndex = (gcodeWriteIndex + 1u) % gcodeBufLength;
	  }
	  while (c != 0);
  }
}

//********************************************************************************************

// Communications with the client

//--------------------------------------------------------------------------------------------

// Output to the client

void Webserver::CloseClient()
{
  writing = false;
  //inPHPFile = false;
  //InitialisePHP();
  clientCloseTime = platform->Time();
  needToCloseClient = true;   
}


void Webserver::SendFile(const char* nameOfFileToSend)
{
  char sLen[SHORT_STRING_LENGTH];
  bool zip = false;
    
  if(StringStartsWith(nameOfFileToSend, KO_START))
    GetJsonResponse(&nameOfFileToSend[KO_FIRST]);
    
  if(jsonPointer < 0)
  {
    fileBeingSent = platform->GetFileStore(platform->GetWebDir(), nameOfFileToSend, false);
    if(fileBeingSent == NULL)
    {
      nameOfFileToSend = FOUR04_FILE;
      fileBeingSent = platform->GetFileStore(platform->GetWebDir(), nameOfFileToSend, false);
    }
    writing = fileBeingSent != NULL;
  } 
  
  platform->GetNetwork()->Write("HTTP/1.1 200 OK\n");
  
  platform->GetNetwork()->Write("Content-Type: ");
  
  if(StringEndsWith(nameOfFileToSend, ".png"))
	  platform->GetNetwork()->Write("image/png\n");
  else if(StringEndsWith(nameOfFileToSend, ".ico"))
	  platform->GetNetwork()->Write("image/x-icon\n");
  else if (jsonPointer >=0)
	  platform->GetNetwork()->Write("application/json\n");
  else if(StringEndsWith(nameOfFileToSend, ".js"))
	  platform->GetNetwork()->Write("application/javascript\n");
  else if(StringEndsWith(nameOfFileToSend, ".zip"))
  {
	  platform->GetNetwork()->Write("application/zip\n");
    zip = true;
  } else
	  platform->GetNetwork()->Write("text/html\n");
    
  if (jsonPointer >=0)
  {
	  platform->GetNetwork()->Write("Content-Length: ");
    snprintf(sLen, SHORT_STRING_LENGTH, "%d", strlen(jsonResponse));
    platform->GetNetwork()->Write(sLen);
    platform->GetNetwork()->Write("\n");
  }
    
  if(zip)
  {
	platform->GetNetwork()->Write("Content-Encoding: gzip\n");
	platform->GetNetwork()->Write("Content-Length: ");
    snprintf(sLen, SHORT_STRING_LENGTH, "%llu", fileBeingSent->Length());
    platform->GetNetwork()->Write(sLen);
    platform->GetNetwork()->Write("\n");
  }
    
  platform->GetNetwork()->Write("Connection: close\n");

  platform->GetNetwork()->Write('\n');
}

void Webserver::WriteByte()
{
    if(jsonPointer >= 0)
    {
      if(jsonResponse[jsonPointer])
      {
    	  platform->GetNetwork()->Write(jsonResponse[jsonPointer++]);
      }
      else
      {
        jsonPointer = -1;
        jsonResponse[0] = 0;
        CloseClient();
      }
    } else
    {
      char b;
      if(fileBeingSent->Read(b))
      {
    	  platform->GetNetwork()->Write(b);
      }
      else
      {
        fileBeingSent->Close();
        CloseClient();
      }
    }
}

//----------------------------------------------------------------------------------------------------

// Input from the client

void Webserver::CheckPassword()
{
  gotPassword = StringEndsWith(clientQualifier, password);
}

void Webserver::JsonReport(bool ok, const char* request)
{
  if(ok)
  {
    if(reprap.Debug())
    {
      platform->Message(HOST_MESSAGE, "JSON response: ");
      platform->Message(HOST_MESSAGE, jsonResponse);
      platform->Message(HOST_MESSAGE, " queued\n");
    }
  } else
  { 
    platform->Message(HOST_MESSAGE, "KnockOut request: ");
    platform->Message(HOST_MESSAGE, request);
    platform->Message(HOST_MESSAGE, " not recognised\n");
    clientRequest[0] = 0;
  } 
}

void Webserver::GetJsonResponse(const char* request)
{
  jsonPointer = 0;
  writing = true;
  
  if(StringStartsWith(request, "poll"))
  {
    strncpy(jsonResponse, "{\"poll\":[", STRING_LENGTH);
    if(reprap.GetGCodes()->PrintingAFile())
    	strncat(jsonResponse, "\"P\",", STRING_LENGTH); // Printing
    else
    	strncat(jsonResponse, "\"I\",", STRING_LENGTH); // Idle
    for(int8_t heater = 0; heater < HEATERS; heater++)
    {
      strncat(jsonResponse, "\"", STRING_LENGTH);
      strncat(jsonResponse, ftoa(0, reprap.GetHeat()->GetTemperature(heater), 1), STRING_LENGTH);
      strncat(jsonResponse, "\",", STRING_LENGTH);
    }
    float liveCoordinates[DRIVES+1];
    reprap.GetMove()->LiveCoordinates(liveCoordinates);
    for(int8_t drive = 0; drive < AXES; drive++)
    {
    	strncat(jsonResponse, "\"", STRING_LENGTH);
    	strncat(jsonResponse, ftoa(0, liveCoordinates[drive], 2), STRING_LENGTH);
    	strncat(jsonResponse, "\",", STRING_LENGTH);
    }

    // FIXME: should loop through all Es

    strncat(jsonResponse, "\"", STRING_LENGTH);
    strncat(jsonResponse, ftoa(0, liveCoordinates[AXES], 4), STRING_LENGTH);
    strncat(jsonResponse, "\"", STRING_LENGTH);
    strncat(jsonResponse, "]", STRING_LENGTH);

    // Send the Z probe value
    char scratch[SHORT_STRING_LENGTH+1];
    scratch[SHORT_STRING_LENGTH] = 0;
    if (platform->GetZProbeType() == 2)
    {
    	snprintf(scratch, SHORT_STRING_LENGTH, ",\"probe\":\"%d (%d)\"", (int)platform->ZProbe(), platform->ZProbeOnVal());
    }
    else
    {
    	snprintf(scratch, SHORT_STRING_LENGTH, ",\"probe\":\"%d\"", (int)platform->ZProbe());
    }
    strncat(jsonResponse, scratch, STRING_LENGTH);

    // Send the amount of buffer space available for gcodes
   	snprintf(scratch, SHORT_STRING_LENGTH, ",\"buff\":%u", GetReportedGcodeBufferSpace());
   	strncat(jsonResponse, scratch, STRING_LENGTH);

    // Send the home state. To keep the messages short, we send 1 for homed and 0 for not homed, instead of true and false.
    strncat(jsonResponse, ",\"hx\":", STRING_LENGTH);
    strncat(jsonResponse, (reprap.GetGCodes()->GetAxisIsHomed(0)) ? "1" : "0", STRING_LENGTH);
    strncat(jsonResponse, ",\"hy\":", STRING_LENGTH);
    strncat(jsonResponse, (reprap.GetGCodes()->GetAxisIsHomed(1)) ? "1" : "0", STRING_LENGTH);
    strncat(jsonResponse, ",\"hz\":", STRING_LENGTH);
    strncat(jsonResponse, (reprap.GetGCodes()->GetAxisIsHomed(2)) ? "1" : "0", STRING_LENGTH);

    // Send the response sequence number
    strncat(jsonResponse, ",\"seq\":", STRING_LENGTH);
	snprintf(scratch, SHORT_STRING_LENGTH, "%u", (unsigned int)seq);
	strncat(jsonResponse, scratch, STRING_LENGTH);

    // Send the response to the last command. Do this last because it is long and may need to be truncated.
    strncat(jsonResponse, ",\"resp\":\"", STRING_LENGTH);
    size_t jp = strnlen(jsonResponse, STRING_LENGTH);
    const char *p = gcodeReply;
    while (*p != 0 && jp < STRING_LENGTH - 2)	// leave room for the final '"}'
    {
    	char c = *p++;
    	char esc;
    	switch(c)
    	{
    	case '\r':
    		esc = 'r'; break;
    	case '\n':
    		esc = 'n'; break;
    	case '\t':
    		esc = 't'; break;
    	case '"':
    		esc = '"'; break;
    	case '\\':
    		esc = '\\'; break;
    	default:
    		esc = 0; break;
    	}
    	if (esc)
    	{
    		if (jp == STRING_LENGTH - 3)
    			break;
   			jsonResponse[jp++] = '\\';
   			jsonResponse[jp++] = esc;
    	}
    	else
    	{
    		jsonResponse[jp++] = c;
    	}
    }
    strncat(jsonResponse, "\"}", STRING_LENGTH);

    jsonResponse[STRING_LENGTH] = 0;
    JsonReport(true, request);
    return;
  }
  
  if(StringStartsWith(request, "gcode"))
  {
    if(!LoadGcodeBuffer(&clientQualifier[6], true))
    {
      platform->Message(HOST_MESSAGE, "Webserver: buffer not free!\n");
    }
    char scratch[SHORT_STRING_LENGTH+1];
    scratch[SHORT_STRING_LENGTH] = 0;
    snprintf(scratch, SHORT_STRING_LENGTH, "{\"buff\":%u}", GetReportedGcodeBufferSpace());
   	strncat(jsonResponse, scratch, STRING_LENGTH);
    JsonReport(true, request);
    return;
  }
  
  if(StringStartsWith(request, "files"))
  {
    char* fileList = platform->GetMassStorage()->FileList(platform->GetGCodeDir(), false);
    strncpy(jsonResponse, "{\"files\":[", STRING_LENGTH);
    strncat(jsonResponse, fileList, STRING_LENGTH);
    strncat(jsonResponse, "]}", STRING_LENGTH);
    JsonReport(true, request);
    return;
  }
  
  if(StringStartsWith(request, "name"))
  {
    strncpy(jsonResponse, "{\"myName\":\"", STRING_LENGTH);
    strncat(jsonResponse, myName, STRING_LENGTH);
    strncat(jsonResponse, "\"}", STRING_LENGTH);
    JsonReport(true, request);
    return;
  }
  
  if(StringStartsWith(request, "password"))
  {
    CheckPassword();
    strncpy(jsonResponse, "{\"password\":\"", STRING_LENGTH);
    if(gotPassword)
      strncat(jsonResponse, "right", STRING_LENGTH);
    else
      strncat(jsonResponse, "wrong", STRING_LENGTH);
    strncat(jsonResponse, "\"}", STRING_LENGTH);
    JsonReport(true, request);
    return;
  }
  
  if(StringStartsWith(request, "axes"))
  {
    strncpy(jsonResponse, "{\"axes\":[", STRING_LENGTH);
    for(int8_t drive = 0; drive < AXES; drive++)
    {
      strncat(jsonResponse, "\"", STRING_LENGTH);
      strncat(jsonResponse, ftoa(0, platform->AxisLength(drive), 1), STRING_LENGTH);
      if(drive < AXES-1)
        strncat(jsonResponse, "\",", STRING_LENGTH);
      else
        strncat(jsonResponse, "\"", STRING_LENGTH);
    }
    strncat(jsonResponse, "]}", STRING_LENGTH);
    JsonReport(true, request);
    return;
  }
  
  JsonReport(false, request);
}

/*

Parse a string in clientLine[] from the user's web browser

Simple requests have the form:

GET /page2.htm HTTP/1.1
     ^  Start clientRequest[] at clientLine[5]; stop at the blank or...

...fancier ones with arguments after a '?' go:

GET /gather.asp?pwd=my_pwd HTTP/1.1
     ^ Start clientRequest[]
                ^ Start clientQualifier[]
*/

void Webserver::ParseGetPost()
{
    if(reprap.Debug())
    {
      platform->Message(HOST_MESSAGE, "HTTP request: ");
      platform->Message(HOST_MESSAGE, clientLine);
      platform->Message(HOST_MESSAGE, "\n");
    }
    
    int i = 5;
    int j = 0;
    clientRequest[j] = 0;
    clientQualifier[0] = 0;
    while(clientLine[i] != ' ' && clientLine[i] != '?')
    {
      clientRequest[j] = clientLine[i];
      j++;
      i++;
    }
    clientRequest[j] = 0;
    if(clientLine[i] == '?')
    {
      i++;
      j = 0;
      while(clientLine[i] != ' ')
      {
        clientQualifier[j] = clientLine[i];
        j++;
        i++;      
      }
      clientQualifier[j] = 0;
    } 
}

void Webserver::InitialisePost()
{
  postSeen = false;
  receivingPost = false;
  boundaryCount = 0; 
  postBoundary[0] = 0;
  postFileName[0] = 0;
  postFile = NULL;
}

void Webserver::ParseClientLine()
{ 
  if(StringStartsWith(clientLine, "GET"))
  {
    ParseGetPost();
    postSeen = false;
    getSeen = true;
    if(!clientRequest[0])
      strncpy(clientRequest, INDEX_PAGE, STRING_LENGTH);
    return;
  }
  
  if(StringStartsWith(clientLine, "POST"))
  {
    ParseGetPost();
    InitialisePost();
    postSeen = true;
    getSeen = false;
    if(!clientRequest[0])
      strncpy(clientRequest, INDEX_PAGE, STRING_LENGTH);
    return;
  }
  
  int bnd;
  
  if(postSeen && ( (bnd = StringContains(clientLine, "boundary=")) >= 0) )
  {
    if(strlen(&clientLine[bnd]) >= POST_LENGTH - 4)
    {
      platform->Message(HOST_MESSAGE, "Post boundary buffer overflow.\n");
      return;
    }
    postBoundary[0] = '-';
    postBoundary[1] = '-';
    strncpy(&postBoundary[2], &clientLine[bnd], POST_LENGTH - 3);
    strncat(postBoundary, "--", POST_LENGTH);
    //Serial.print("Got boundary: ");
    //Serial.println(postBoundary);
    return;
  }
  
  if(receivingPost && StringStartsWith(clientLine, "Content-Disposition:"))
  {
    bnd = StringContains(clientLine, "filename=\"");
    if(bnd < 0)
    {
      platform->Message(HOST_MESSAGE, "Post disposition gives no filename.\n");
      return;
    }
    int i = 0;
    while(clientLine[bnd] && clientLine[bnd] != '"')
    {
      postFileName[i++] = clientLine[bnd++];
      if(i >= POST_LENGTH)
      {
        i = 0;
        platform->Message(HOST_MESSAGE, "Post filename buffer overflow.\n");
      }
    }
    postFileName[i] = 0;
    //Serial.print("Got file name: ");
    //Serial.println(postFileName);    
    return;
  }  
}

// if you've gotten to the end of the line (received a newline
// character) and the line is blank, the http request has ended,
// so you can send a reply
void Webserver::BlankLineFromClient()
{
  clientLine[clientLinePointer] = 0;
  clientLinePointer = 0;
  //ParseQualifier();
  
  //Serial.println("End of header.");
  
  // Soak up any rubbish on the end.

  char c;
  while(platform->GetNetwork()->Read(c));


  if(getSeen)
  {
    SendFile(clientRequest);
    clientRequest[0] = 0;
    return;
  }
  
  if(postSeen)
  {
    receivingPost = true;
    postSeen = false;
    return;
  }
  
  if(receivingPost)
  {
    postFile = platform->GetFileStore(platform->GetGCodeDir(), postFileName, true);
    if(postFile == NULL  || !postBoundary[0])
    {
      platform->Message(HOST_MESSAGE, "Can't open file for write or no post boundary: ");
      platform->Message(HOST_MESSAGE, postFileName);
      platform->Message(HOST_MESSAGE, "\n");
      InitialisePost();
      if(postFile != NULL)
        postFile->Close();
    }
  }
}

void Webserver::CharFromClient(char c)
{
  if(c == '\n' && clientLineIsBlank) 
  {
    BlankLineFromClient();
    return;
  }
  
  if(c == '\n') 
  {
    clientLine[clientLinePointer] = 0;
    ParseClientLine();
    // you're starting a new line
    clientLineIsBlank = true;
    clientLinePointer = 0;
  } else if(c != '\r') 
  {
    // you've gotten a character on the current line
    clientLineIsBlank = false;
    clientLine[clientLinePointer] = c;
    clientLinePointer++;
    if(clientLinePointer >= STRING_LENGTH)
    {
      platform->Message(HOST_MESSAGE, "Client read buffer overflow. Data:\n");
      clientLine[STRING_LENGTH] = '\n';		// note that clientLine is now STRING_LENGTH+2 characters long to make room for these
      clientLine[STRING_LENGTH+1] = 0;
      platform->Message(HOST_MESSAGE, clientLine);

      clientLinePointer = 0;
      clientLine[clientLinePointer] = 0; 
      clientLineIsBlank = true;
    }
  }  
}

// Deal with input/output from/to the client (if any) one byte at a time.

void Webserver::Spin()
{
  //char sw[2];
  if(!active)
    return;
    
  if(writing)
  {
 //   if(inPHPFile)
 //     WritePHPByte();
 //   else
	// Write several bytes at a time, otherwise the web server is unbearably slow
	for (uint8_t i = 0; i < 10 && writing && platform->GetNetwork()->CanWrite(); ++i)
	{
      WriteByte();
	}
	platform->ClassReport("Webserver", longWait);
    return;
  }
  
  char c;

  if(platform->GetNetwork()->Active())
  {
	  if(platform->GetNetwork()->Status() & clientConnected)
	  {
		  if(platform->GetNetwork()->Status() & byteAvailable)
		  {
			  platform->GetNetwork()->Read(c);
			  //SerialUSB.print(c);

			  if(receivingPost && postFile != NULL)
			  {
				  if(MatchBoundary(c))
				  {
					  //Serial.println("Got to end of file.");
					  postFile->Close();
					  SendFile(clientRequest);
					  clientRequest[0] = 0;
					  InitialisePost();
				  }
				  platform->ClassReport("Webserver", longWait);
				  return;
			  }

			  CharFromClient(c);
		  }
	  }
  }
   
  if (platform->GetNetwork()->Status() & clientLive)
  {
    if(needToCloseClient)
    {
      if(platform->Time() - clientCloseTime < CLIENT_CLOSE_DELAY)
      {
    	platform->ClassReport("Webserver", longWait);
        return;
      }
      needToCloseClient = false;  
      platform->GetNetwork()->Close();
    }   
  }
  platform->ClassReport("Webserver", longWait);
}

//******************************************************************************************

// Constructor and initialisation

Webserver::Webserver(Platform* p)
{ 
  platform = p;
  active = false;
  gotPassword = false;
}

void Webserver::Init()
{
  writing = false;
  receivingPost = false;
  postSeen = false;
  getSeen = false;
  jsonPointer = -1;
  clientLineIsBlank = true;
  needToCloseClient = false;
  clientLinePointer = 0;
  clientLine[0] = 0;
  clientRequest[0] = 0;
  SetPassword(DEFAULT_PASSWORD);
  SetName(DEFAULT_NAME);
  //gotPassword = false;
  gcodeReadIndex = gcodeWriteIndex = 0;
  InitialisePost();
  lastTime = platform->Time();
  longWait = lastTime;
  active = true;
  gcodeReply[0] = 0;
  seq = 0;
  
  // Reinitialise the message file
  
  //platform->GetMassStorage()->Delete(platform->GetWebDir(), MESSAGE_FILE);
}

// This is called when the connection has been lost.
// In particular, we must cancel any pending writes.
void Webserver::ConnectionError()
{
	  writing = false;
	  receivingPost = false;
	  postSeen = false;
	  getSeen = false;
	  jsonPointer = -1;
	  clientLineIsBlank = true;
	  needToCloseClient = false;
	  clientLinePointer = 0;
	  clientLine[0] = 0;
	  clientRequest[0] = 0;
	  gotPassword = false;
	  gcodeReadIndex = gcodeWriteIndex = 0;
	  InitialisePost();
	  lastTime = platform->Time();
	  longWait = lastTime;
	  active = true;

}

void Webserver::Exit()
{
  platform->Message(HOST_MESSAGE, "Webserver class exited.\n");
  active = false;
}

void Webserver::Diagnostics() 
{
  platform->Message(HOST_MESSAGE, "Webserver Diagnostics:\n"); 
}

void Webserver::SetPassword(const char* pw)
{
	strncpy(password, pw, SHORT_STRING_LENGTH);
	password[SHORT_STRING_LENGTH] = 0; // NB array is dimensioned to SHORT_STRING_LENGTH+1
}

void Webserver::SetName(const char* nm)
{
	strncpy(myName, nm, SHORT_STRING_LENGTH);
	myName[SHORT_STRING_LENGTH] = 0; // NB array is dimensioned to SHORT_STRING_LENGTH+1
}

void Webserver::HandleReply(const char *s, bool error)
{
	if (strlen(s) == 0 && !error)
	{
		strcpy(gcodeReply, "ok");
	}
	else
	{
		if (error)
		{
			strcpy(gcodeReply, "Error: ");
			strncat(gcodeReply, s, STRING_LENGTH);
		}
		else
		{
			strncpy(gcodeReply, s, STRING_LENGTH);
		}
		gcodeReply[STRING_LENGTH] = 0;	// array is dimensioned to STRING_LENGTH+1
	}
	++seq;
}

void Webserver::AppendReply(const char *s)
{
	strncat(gcodeReply, s, STRING_LENGTH);
}

// Get the actual amount of gcode buffer space we have
unsigned int Webserver::GetGcodeBufferSpace() const
{
	return (gcodeReadIndex - gcodeWriteIndex - 1u) % gcodeBufLength;
}

// Get the amount of gcode buffer space we are going to report
unsigned int Webserver::GetReportedGcodeBufferSpace() const
{
	unsigned int temp = GetGcodeBufferSpace();
	return (temp > maxReportedFreeBuf) ? maxReportedFreeBuf
			: (temp < minReportedFreeBuf) ? 0
				: temp;
}

