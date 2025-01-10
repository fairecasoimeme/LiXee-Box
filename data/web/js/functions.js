var powerGaugeAbo1;
var powerGaugeAbo2;
var powerGaugeAbo3;
var powerChart;
var energyChart;
function getXhr(){
	var xhr = null; 
	if(window.XMLHttpRequest) // Firefox et autres
	   xhr = new XMLHttpRequest(); 
	else if(window.ActiveXObject){ // Internet Explorer 
	   try {
				xhr = new ActiveXObject("Msxml2.XMLHTTP");
			} catch (e) {
				xhr = new ActiveXObject("Microsoft.XMLHTTP");
			}
	}
	else { // XMLHttpRequest non supporté par le navigateur 
	   alert("Votre navigateur ne supporte pas les objets XMLHTTPRequest..."); 
	   xhr = false; 
	} 
	return xhr;
}

function power(mac,cmd)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			document.getElementById(mac).innerHTML=leselect;
		}
	}
	xhr.open("GET","SetPower?mac="+escape(mac)+"&cmd="+escape(cmd),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
	
}
function GetGSMStatus()
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			document.getElementById("gsmstatus").innerHTML=leselect;
		}
	}
	xhr.open("GET","GetGSMStatus",true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
	
}
function GetThermostatStatus()
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			document.getElementById("thermostat").innerHTML=leselect;
		}
	}
	xhr.open("GET","GetThermostatStatus",true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function GetAction(mac)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			document.getElementById(mac).innerHTML=leselect;
			setTimeout(function(){ GetAction(mac); }, 3000);
		}
	}
	xhr.open("GET","GetAction?mac="+escape(mac),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function ZigbeeAction(shortaddr,command,value)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
		}
	}
	xhr.open("GET","ZigbeeAction?shortaddr="+escape(shortaddr)+"&command="+escape(command)+"&value="+escape(value),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function ZigbeeSendRequest(shortaddr,cluster,attribute)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
		}
	}
	xhr.open("GET","ZigbeeSendRequest?shortaddr="+escape(shortaddr)+"&cluster="+escape(cluster)+"&attribute="+escape(attribute),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function readfile(file,rep)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			document.getElementById("title").innerHTML=file;
			document.getElementById("filename").value=file;
			document.getElementById("file").innerHTML=leselect;
		}
	}
	
	xhr.open("GET","readFile?rep="+escape(rep)+"&file="+escape(file),true);
	
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function logRefresh()
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			document.getElementById("console").value=leselect;
			setTimeout(function(){ logRefresh(); }, 5000);
		}
	}
	xhr.open("GET","getLogBuffer",true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function scanNetwork()
{
	document.getElementById("networks").innerHTML="<img src='/web/img/wait.gif'>";
	var xhr = getXhr();	
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			const datas = leselect.split('|');
			if (parseInt(datas[0])>=0)
			{
				document.getElementById("networks").innerHTML=datas[1];
			}else{
				scanNetwork();
			}
			
		}
	}
	xhr.open("GET","scanNetwork",true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function updateSSID(val)
{
	document.getElementById("ssid").value=val;
}

function cmd(val,param="")
{

	var xhr = getXhr();
	xhr.open("GET","cmd"+val+"?param="+escape(param),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function loadLinkyDatas(IEEE)
{
	var xhr = getXhr();
	document.getElementById("power_data").innerHTML="<img src='/web/img/wait.gif'>";
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			document.getElementById("power_data").innerHTML=leselect;
		}
	}
	xhr.open("GET","loadLinkyDatas?IEEE="+escape(IEEE),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function loadPowerGaugeAbo(phase,IEEE,attribute,time)
{
	var xhr = getXhr();
	
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			const datas = leselect.split(';');
			if (phase==1)
			{
				if (time=='hour')
				{
					labelTime='VA (Ph1)';
				}else{
					labelTime='Wh';
				}
				powerGaugeAbo1 = new JustGage({
						id: 'power_gauge_global',
						value: datas[0],
						min: 0,
						max: datas[1],
						title: 'Target',
						label: labelTime,
						gaugeWidthScale: 0.6,
						pointer: true,
						pointerOptions: {
						toplength: -15,
						bottomlength: 10,
						bottomwidth: 12,
						color: '#8e8e93',
						stroke: '#ffffff',
						stroke_width: 3,
						stroke_linecap: 'round'
						},
						relativeGaugeSize: true,
						refreshAnimationTime: 1000
				});
			}else if (phase==2)
			{
				if (time=='hour')
				{
					labelTime='VA (Ph2)';
				}
				powerGaugeAbo2 = new JustGage({
					id: 'power_gauge_global2',
					value: datas[0],
					min: 0,
					max: datas[1],
					title: 'Target',
					label: labelTime,
					gaugeWidthScale: 0.6,
					pointer: true,
					pointerOptions: {
					toplength: -15,
					bottomlength: 10,
					bottomwidth: 12,
					color: '#8e8e93',
					stroke: '#ffffff',
					stroke_width: 3,
					stroke_linecap: 'round'
					},
					relativeGaugeSize: true,
					refreshAnimationTime: 1000
				});
			}else if (phase==3)
			{
				if (time=='hour')
				{
					labelTime='VA (Ph3)';
				}
				powerGaugeAbo3 = new JustGage({
					id: 'power_gauge_global3',
					value: datas[0],
					min: 0,
					max: datas[1],
					title: 'Target',
					label: labelTime,
					gaugeWidthScale: 0.6,
					pointer: true,
					pointerOptions: {
					toplength: -15,
					bottomlength: 10,
					bottomwidth: 12,
					color: '#8e8e93',
					stroke: '#ffffff',
					stroke_width: 3,
					stroke_linecap: 'round'
					},
					relativeGaugeSize: true,
					refreshAnimationTime: 1000
				});
			}

		}
	}
	xhr.open("GET","loadPowerGaugeAbo?IEEE="+escape(IEEE)+"&attribute="+escape(attribute)+"&time="+escape(time),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function getLabelEnergy(datas,row, barColor, options, index)
{
	var datas = JSON.parse(datas);
	var colors = eval(barColor);
	var result="";
	i=0;
    totalEuro=0;
	total=0;
	var unit="";
	var label = options.data[index];
	result += label.y+"<br>";
	for (var key in datas) 
	{
		if (row[key] !==undefined)
		{
			const item = datas[key];
			unit = item.unit;
			const value = (item.coeff * row[key]);
			
			totalEuro+=(Math.round(value*item.price)/1000);
			totalEuro+=(Math.round(value*item.taxe)/1000);
			total+=(item.coeff * row[key]);
			if (value!=0)
			{
				if (i>0){sep="<br>";}else{sep="";}
				result+= sep +"<span style='color:"+colors[i]+";'>"+ item.name +" : "+value+" "+unit+" / "+(Math.round(value*item.price)/1000)+"€</span>";
			}
		} 
		i++;
	}
    result+="<br><span style='color:red;font-weight:bold;'>Total : "+total+" "+ unit+" / "+Math.round(totalEuro)+" €</span>";
	return result;
  
}

function refreshDashboard(IEEE,attribute,time)
{
	refreshGaugeAbo(IEEE,attribute,time);
	loadPowerTrend(IEEE,attribute,time);
	setTimeout(function(){refreshDashboard(IEEE,attribute,time); }, 60000);
}

function loadGazChart(IEEE,time)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			var datas = JSON.parse(xhr.responseText);
			 gazChart.setData(datas);
		}
	}
	xhr.open("GET","loadEnergyChart?IEEE="+escape(IEEE)+"&time="+escape(time),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function loadWaterChart(IEEE,time)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			var datas = JSON.parse(xhr.responseText);
			waterChart.setData(datas);
		}
	}
	xhr.open("GET","loadEnergyChart?IEEE="+escape(IEEE)+"&time="+escape(time),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function refreshStatusGaz(IEEE,time)
{
	loadGazChart(IEEE,time);
	setTimeout(function(){refreshStatusGaz(IEEE,time); }, 60000);
}

function refreshStatusWater(IEEE,time)
{
	loadWaterChart(IEEE,time);
	setTimeout(function(){refreshStatusWater(IEEE,time); }, 60000);
}

function refreshStatusEnergy(IEEE,attribute,time)
{
	refreshGaugeAbo(IEEE,attribute,time);
	loadPowerTrend(IEEE,attribute,time);
	loadLinkyDatas(IEEE);
	if (time=='hour')
	{
		loadPowerChart(IEEE,attribute);
	}
	loadEnergyChart(IEEE,time);
	setTimeout(function(){refreshStatusEnergy(IEEE,attribute,time); }, 60000);
}

function refreshLabel(file,shortaddr,cluster,attribute,type,coefficient,unit)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			document.getElementById("label_"+shortaddr+"_"+cluster+"_"+attribute).innerHTML=leselect;
			setTimeout(function(){ refreshLabel(file,shortaddr,cluster,attribute,type,coefficient,unit); }, 60000);
		}
	}
	xhr.open("GET","refreshLabel?file="+escape(file)+"&cluster="+escape(cluster)+"&attribute="+escape(attribute)+"&type="+escape(type)+"&coeff="+escape(coefficient)+"&unit="+escape(unit),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function refreshGaugeAbo(IEEE,attribute,time)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			//document.getElementById(mac).innerHTML=leselect;
			powerGaugeAbo1.refresh(leselect);
			//setTimeout(function(){ refreshGaugeAbo(IEEE,attribute);loadPowerTrend(IEEE,attribute);loadLinkyDatas(IEEE);loadPowerChart(IEEE,attribute);loadEnergyChart(IEEE,'hour');}, 60000);
		}
	}
	xhr.open("GET","refreshGaugeAbo?IEEE="+escape(IEEE)+"&attribute="+escape(attribute)+"&time="+escape(time),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function loadPowerTrend(IEEE,attribute,time)
{
	var xhr = getXhr();
	document.getElementById("power_trend").innerHTML="<img src='/web/img/wait.gif'>";
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			
			leselect = xhr.responseText;
			document.getElementById("power_trend").innerHTML=leselect;
			//setTimeout(function(){ loadPowerTrend(IEEE,attribute); }, 60000);
		}
	}
	xhr.open("GET","loadPowerTrend?IEEE="+escape(IEEE)+"&attribute="+escape(attribute)+"&time="+escape(time),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function loadPowerGaugeTimeDay(IEEE,attribute)
{
	var xhr = getXhr();
	document.getElementById("power_gauge_day").innerHTML="<img src='/web/img/wait.gif'>";
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			const datas = leselect.split(';');
			var powerGaugeTimeDay = new JustGage({
					id: 'power_gauge_day',
					value: datas[0],
					min: datas[1],
					max: datas[2],
					title: 'Target',
					label: 'VA',
					gaugeWidthScale: 0.6,
					pointer: true,
					pointerOptions: {
					   toplength: 10,
					   bottomlength: 10,
					   bottomwidth: 2
					},
					humanFriendly: true,
					relativeGaugeSize: true,
					refreshAnimationTime: 1000
              });
		}
	}
	xhr.open("GET","loadPowerGaugeTimeDay?IEEE="+escape(IEEE)+"&attribute="+escape(attribute),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function loadGaugeDashboard(div,IEEE,cluster,attribute,type,coefficient,min,max,label)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			var Gauge = new JustGage({
						id: div,
						value: leselect,
						min: min,
						max: max,
						title: 'Target',
						label: label,
						gaugeWidthScale: 0.6,
						pointer: true,
						pointerOptions: {
						   toplength: 10,
						   bottomlength: 10,
						   bottomwidth: 2
						},
						humanFriendly: true,
						relativeGaugeSize: true,
						refreshAnimationTime: 1000
				  });	
			setTimeout(function(){ loadGaugeDashboard(div,IEEE,cluster,attribute,type,coefficient,min,max,label) }, 60000);
		}
	}
	xhr.open("GET","loadGaugeDashboard?IEEE="+escape(IEEE)+"&cluster="+escape(cluster)+"&attribute="+escape(attribute)+"&type="+escape(type)+"&coefficient="+escape(coefficient),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}


function loadPowerChart(IEEE,attribute)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			var datas = JSON.parse(xhr.responseText);
			//powerChart.setData(datas);
			powerChart.setData(datas["datas"]);
		}
	}
	xhr.open("GET","loadPowerChart?IEEE="+escape(IEEE)+"&attribute="+escape(attribute),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function loadEnergyChart(IEEE,time)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			var datas = JSON.parse(xhr.responseText);
			 energyChart.setData(datas);
		}
	}
	xhr.open("GET","loadEnergyChart?IEEE="+escape(IEEE)+"&time="+escape(time),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}


function getFormattedDate()
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			document.getElementById('FormattedDate').innerHTML=leselect;
			setTimeout(function(){ getFormattedDate(); }, 60000);
		}
	}
	xhr.open('GET','getFormattedDate',true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function deleteDevice(devId)
{
	
	const response = confirm("Are you sure you want to delete this device ?");
	if (response){
		var xhr = getXhr();
		xhr.onreadystatechange = function(){
			if(xhr.readyState == 4 ){
				leselect = xhr.responseText;
			}
		}
		xhr.open("GET","deleteDevice?devId="+escape(devId),true);
		xhr.setRequestHeader('Content-Type','application/html');
		xhr.send();
	}
	  
}

var pause;
function getAlert()
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			if (leselect!="")
			{
				const datas = leselect.split(';');
				if (datas[0]==0)
				{
					var element = document.getElementById("alert");
					element.classList.remove("alert-danger");
					element.classList.add("alert-success");
					document.getElementById('alert').innerHTML=datas[1];
					document.getElementById('alert').style.display='block';
					pause = 0;
				}else if (datas[0]==1){
					var element = document.getElementById("alert");
					element.classList.remove("alert-success");
					element.classList.add("alert-danger");
					document.getElementById('alert').innerHTML=datas[1];
					document.getElementById('alert').style.display='block';
					pause = 0;
				}else if (datas[0]==2){
					var element = document.getElementById("alert");
					element.classList.remove("alert-danger");
					element.classList.add("alert-success");
					document.getElementById('alert').innerHTML=datas[1];
					document.getElementById('alert').style.display='block';
					pause=6; 
					setTimeout(function(){ location.reload();}, 30000);
				}
			}else{
				if (pause>0){pause--;}
				if (pause==0){
					document.getElementById('alert').style.display='none';
				}	
			}
			setTimeout(function(){ getAlert(); }, 5000);
		}
	}
	xhr.open('GET','getAlert',true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function getLatestReleaseInfo() {
	$.getJSON("https://api.github.com/repos/fairecasoimeme/lixee-box/releases/latest").done(function(release) {
	  var asset = release.assets[0];
	  var downloadCount = 0;
	  for (var i = 0; i < release.assets.length; i++) {
		downloadCount += release.assets[i].download_count;
	  }
	  var oneHour = 60 * 60 * 1000;
	  var oneDay = 24 * oneHour;
	  var dateDiff = new Date() - new Date(release.published_at);
	  var timeAgo;
	  if (dateDiff < oneDay) {
		timeAgo = (dateDiff / oneHour).toFixed(1) + " hours ago";
	  } else {
		timeAgo = (dateDiff / oneDay).toFixed(1) + " days ago";
	  }

	  var releaseInfo = release.name + " was updated " + timeAgo + " and downloaded " + downloadCount.toLocaleString() + " times.";
	  $("#downloadupdate").attr("href", asset.browser_download_url);
	  $("#releasehead").text(releaseInfo);
	  $("#releasebody").text(release.body);
	  $("#releaseinfo").fadeIn("slow");
	});
  }

  function toggleDiv(div) {
	var x = document.getElementById(div);
	if (x.style.display === "none") {
	  x.style.display = "block";
	} else {
	  x.style.display = "none";
	}
  } 

function createBackupFile()
{
	var xhr = getXhr();
	document.getElementById('createBackupFile').innerHTML="<img src='/web/img/wait.gif'>";
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			document.getElementById('createBackupFile').innerHTML=leselect;
		}
	}
	xhr.open('GET','createBackupFile',true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function sendMqttDiscover(shortaddr)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
		}
	}
	xhr.open("GET","sendMqttDiscover?shortAddr="+escape(shortaddr),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}
  