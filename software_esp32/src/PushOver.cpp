/**HEADER*******************************************************************
  project : VdMot Controller

  author : SurfGargano, Lenti84

  Comments: derived from https://github.com/brunojoyal/PushoverESP32

  Version :

  Modifcations :


***************************************************************************
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE DEVELOPER OR ANY CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
* INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
* IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
* THE POSSIBILITY OF SUCH DAMAGE.
*
**************************************************************************
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License.
  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  Copyright (C) 2021 Lenti84  https://github.com/Lenti84/VdMot_Controller

*END************************************************************************/



#include <Arduino.h>
#include "Pushover.h"
#include <map>
#include "globals.h"
#include "VdmTask.h"

const char *PUSHOVER_API_URL = "https://api.pushover.net/1/messages.json";

CPushover::CPushover()
{
	client.setInsecure();
}

CPushover::CPushover(const char *token, const char *user) : _token(token), _user(user)
{
	client.setInsecure();
}

int CPushover::send(CPushoverMessage newMessage)
{

	int responseCode = 0;
	bool beginResult = false;
	std::map<const char *, const char *> messageData;
	messageData["token"] = _token;
	messageData["user"] = _user;
	messageData["message"] = newMessage.message;
	messageData["title"] = newMessage.title;
	//messageData["url"] = newMessage.url;
	//messageData["url_title"] = newMessage.url_title;
	//messageData["html"] = newMessage.html?"1":"0";
	//messageData["priority"] = ((String)newMessage.priority).c_str();
	//messageData["sound"] = newMessage.sound;
	//messageData["timestamp"] = ((String)newMessage.timestamp).c_str();
	 //No attachment, so just a regular HTTPS POST request.
	//WiFiClientSecure client;
	
	//client.setInsecure();
	if (client.connected()) UART_DBG.println("Pushover already connected");
	uint8_t loop = 0;
	while (!client.connected()) {
		UART_DBG.println("Pushover try connect #"+String(loop+1));
		if (client.connect("api.pushover.net", 443)) break;
		VdmTask.yieldTask(1000);
		loop++;
		if ((loop>=5) && (!client.connected())) {
			UART_DBG.println("Pushover port 443 not connected");
			return -1;
		}
	}
	
	beginResult = myClient.begin(client,PUSHOVER_API_URL);
	myClient.addHeader("Content-Type", "application/json");
	myClient.addHeader("Connection","close");
	StaticJsonDocument<512> doc;
	std::map<const char *, const char *>::iterator it = messageData.begin();
	while(it!=messageData.end()){
		doc[it->first] = it->second;
		it++;
	}
	char postmessage[512];
	serializeJson(doc, postmessage);
	UART_DBG.println("Pushover output :"+String(beginResult)+" > "+String(postmessage));
	responseCode = myClient.POST(postmessage);
	myClient.end();
	return responseCode;
}

void CPushover::setToken(const char *token)
{
	_token = token;
}

void CPushover::setUser(const char *user)
{
	_user = user;
}