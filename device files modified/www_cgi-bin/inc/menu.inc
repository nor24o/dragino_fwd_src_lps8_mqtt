<div id="pageLoader" class="pageLoader">
  Please wait...<br><br>
  <div class="loaderSpinner"><span class="fas fa-spinner fa-spin"></span></div>
</div>

<% 
	vendor_url=$(uci get system.vendor.company_url) 
	model=$(cat /tmp/iot/model.txt)
%>

<div class="logo-container">
  <a href="<% echo $vendor_url %>">  <!-- Set the URL link for the logo -->
    <img class="logoImage" src="/static/img/logo.png">
  </a>
</div>

<div class="topnav" id="docTopnav">

  <div></div> <!-- Space for the logo -->

	<% # Set up LoRa menu for board type
	board=$(cat /var/iot/board)
	if [ "$model" == "LG308" ] || [ "$model" == "LPS8" ] ||  [ "$model" == "LPS8-N" ] || [ "$model" == "DLOS8" ] || [ "$model" == "LIG16" ]|| [ "$model" == "LPS8-G" ]|| [ "$model" == "DLOS8N" ]|| [ "$model" == "LG308-N" ]; then
	#if [ "$board" == "LG08" ] || [ "$board" == "LG08P" ]; then
	%>
  <div class="dropdown"> <!-- LoRa Parameter -->
    <button class="dropbtn">LoRa <span class="fas fa-caret-down"></span></button>
    <div class="dropdown-content">
      <a href="/cgi-bin/lora-lora.has">LoRa</a>
      <a href="/cgi-bin/lora-abp.has">ABP Decryption</a>
    </div>
  </div>
  <% else %>
  <a href="/cgi-bin/lora-lora.has">LoRa</a>

  <% fi %>
  
  
	<% # Set up LoRaWAN menu for board type
	if [ $model == "LG308" ] ||  [ "$model" == "LPS8" ] ||  [ "$model" == "LPS8-N" ]  || [ $model == "DLOS8" ] || [ $model == "LIG16" ] || [ "$model" == "LPS8-G" ] || [ "$model" == "DLOS8N" ]|| [ "$model" == "LG308-N" ]; then
	%>
		<div class="dropdown"> <!-- LoRa Parameter -->
			<button class="dropbtn">LoRaWAN <span class="fas fa-caret-down"></span></button>
			<div class="dropdown-content">
				<a href="/cgi-bin/lorawan.has">LoRaWAN -- Semtech UDP</a>
				

				<% # Set up LoRaWAN menu for board type
				if [ $model == "LIG16" ] || [ "$model" == "LG308" ] || [ "$model" == "LPS8" ] ||  [ "$model" == "LPS8-N" ] || [ "$model" == "DLOS8" ] || [ "$model" == "LPS8-G" ] || [ "$model" == "DLOS8N" ]|| [ "$model" == "LG308-N" ]; then
				%>
					<a href="/cgi-bin/lorawan-basicstation.has">LoRaWAN -- Basic Station</a>
					<a href="/cgi-bin/lorawan-helium.has">Helium IoT</a>
				<% fi %>
				<% # Set up LoRaWAN menu for board type
				if  [ "$model" == "LG308" ] || [ "$model" == "LPS8" ] || [ "$model" == "DLOS8" ]; then
				%>
				<a href="/cgi-bin/loriot.has">LORIOT</a>
				<% fi %>	
			</div>
		</div>
	<% else %>  
		<a href="/cgi-bin/lorawan.has">LoRaWAN</a>
		<a href="/cgi-bin/http-client.has">HTTP</a>
	<% fi %>


  <div class="dropdown"> <!-- MQTT -->
	<button class="dropbtn">MQTT <span class="fas fa-caret-down"></span></button>
    <div class="dropdown-content">
      <a href="/cgi-bin/mqtt.has">MQTT Client</a>
      <a href="/cgi-bin/mqtt-chan.has">Channels</a>
      <a href="/cgi-bin/iot-cert.has">Certificates</a>
    </div>
  </div> 
  
	<div class="dropdown"> <!-- TCP -->
		<button class="dropbtn">TCP <span class="fas fa-caret-down"></span></button>
		<div class="dropdown-content">
			<a href="/cgi-bin/tcp-client.has">TCP Client</a>
		</div>
	</div> 
  
  <a href="/cgi-bin/custom.has">Custom</a>
 
 <div class="dropdown"> <!-- Network -->
    <button class="dropbtn">Network <span class="fas fa-caret-down"></span></button>
    <div class="dropdown-content">
		<a href="/cgi-bin/system-status.has">Network Status</a>
		<a href="/cgi-bin/system-network.has">Network</a>
		<a href="/cgi-bin/system-wifi.has">WiFi</a>
		<a href="/cgi-bin/system-cellular.has">Cellular</a>
    </div>
  </div> 

  <div class="dropdown">
    <button class="dropbtn">OpenVPN<span class="fas fa-caret-down"></span></button>
    <div class="dropdown-content">
                <a href="/cgi-bin/openvpn-client.has">Client</a>
                <a href="/cgi-bin/openvpn-certificates.has">Certificates</a>
    </div>
  </div> 
 
 <div class="dropdown"> <!-- System -->
    <button class="dropbtn">System <span class="fas fa-caret-down"></span></button>
    <div class="dropdown-content">
		<a href="/cgi-bin/system-overview.has">System Overview</a>
		<a href="/cgi-bin/system-general.has">General</a>
		<a href="/cgi-bin/system-config.has">Back Up / Restore Config</a>
		<a href="/cgi-bin/system-remoteit.has">Remote.it</a>
		<a href="/cgi-bin/system-wireguard.has">WireGuard</a>
		<a href="/cgi-bin/system-rssh.has">Remote Mgmt</a>
		<a href="/cgi-bin/system-firmware.has">Firmware Upgrade</a>
		<a href="/cgi-bin/system-reboot.has">Reboot / Reset</a>
		<a href="/cgi-bin/system-package.has">Package Maintain</a>
    </div>
  </div> 
 
 <div class="dropdown"> <!-- LogRead -->
    <button class="dropbtn">LogRead<span class="fas fa-caret-down"></span></button>
    <div class="dropdown-content">
      <a href="/cgi-bin/log-lora.has">LoRa Log</a>
	  
	<% # Set up LoRaWAN menu for board type
	if [ $model == "LG308" ] ||  [ "$model" == "LPS8" ] ||  [ "$model" == "LPS8-N" ]  || [ $model == "DLOS8" ] || [ $model == "LIG16" ] || [ "$model" == "LPS8-G" ] || [ "$model" == "DLOS8N" ]|| [ "$model" == "LG308-N" ] ; then
	%>
      <a href="/cgi-bin/log-traffic.has">Gateway Traffic</a>
	<% fi %>	
	
      <a href="/cgi-bin/log-sys.has">System Log</a>
      <a href="/cgi-bin/log-record.has">Record Log</a>
    </div>
  </div> 

  <a href="/cgi-bin/home.has">Home</a>

  <a href="/cgi-bin/logout.has">Logout</a>

  <div id="refreshIndicate" style="display: none;">AUTO<br>REFRESH</div>

  <a href="javascript:void(0);" style="font-size:15px;" class="icon" onclick="toggleNavbar()">&#9776;</a>
  
</div>