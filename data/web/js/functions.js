var powerGaugeAbo1;
var powerGaugeAbo2;
var powerGaugeAbo3;
var powerChart;
var energyChart;
var donutChart;

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
	else { // XMLHttpRequest non supportÃ© par le navigateur 
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

function scanNetwork(ret)
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

				setTimeout(function(){ scanNetwork(0); }, 1000);
			}
			
		}
	}
	xhr.open("GET","scanNetwork?ret="+escape(ret),true);
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
	//document.getElementById("power_data").innerHTML="<img src='/web/img/wait.gif'>";
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
					labelTime= Math.floor(datas[2]) + ' W';
					powerGaugeAbo1 = new JustGage({
						id: 'power_gauge_global',
						value: datas[0],
						min: 0,
						max: datas[1],
						title: 'Target',
						label: labelTime,
						gaugeWidthScale: 0.6,
						pointer: true,
						textRenderer: function (val) {
							return val+' VA';
						},
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
				}else{
					labelTime='Wh';
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
				}
				
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
				result+= sep +"<span style='color:"+colors[i]+";'>"+ item.name +" : "+value+" "+unit+" / "+(Math.round(value*item.price)/1000)+" €</span>";
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

function loadProductionChart(IEEE,time)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			var datas = JSON.parse(xhr.responseText);
			productionChart.setData(datas);
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

function refreshStatusProduction(IEEE,time)
{
	loadProductionChart(IEEE,time);
	setTimeout(function(){refreshStatusProduction(IEEE,time); }, 60000);
}

function refreshStatusEnergy(IEEE,attribute,time)
{
	//refreshGaugeAbo(IEEE,attribute,time);
	loadPowerTrend(IEEE,attribute,time);
	loadDatasTrend(IEEE,attribute,time);
	loadLinkyDatas(IEEE);
	loadEnergyChart(IEEE,time);
	loadDistributionChart(IEEE,time);
	if (time=='hour')
	{
		loadPowerChart(IEEE,attribute);
		setTimeout(function(){refreshStatusEnergy(IEEE,attribute,time); }, 15000);
	}else{
		setTimeout(function(){refreshStatusEnergy(IEEE,attribute,time); }, 60000);
	}
}

function refreshLabel(file,shortaddr,cluster,attribute,type,coefficient,unit)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			document.getElementById("label_"+shortaddr+"_"+cluster+"_"+attribute).innerHTML=leselect;
			setTimeout(function(){ refreshLabel(file,shortaddr,cluster,attribute,type,coefficient,unit); }, 5000);
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
			const datas = leselect.split(';');
			if (attribute == "1295")
			{
				powerGaugeAbo1.refresh(datas[0],datas[1],datas[2],datas[3]);
			}else if (attribute == "2319")
			{
				powerGaugeAbo2.refresh(datas[0],datas[1],datas[2],datas[3]);
			}else if (attribute == "2575")
			{
				powerGaugeAbo3.refresh(datas[0],datas[1],datas[2],datas[3]);
			}
			setTimeout(function(){refreshGaugeAbo(IEEE,attribute,time); },15000);
		}
	}
	xhr.open("GET","refreshGaugeAbo?IEEE="+escape(IEEE)+"&attribute="+escape(attribute)+"&time="+escape(time),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function loadPowerTrend(IEEE,attribute,time)
{
	var xhr = getXhr();
	//document.getElementById("power_trend").innerHTML="<img src='/web/img/wait.gif'>";
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			
			leselect = xhr.responseText;
			document.getElementById("power_trend").innerHTML=leselect;
		}
	}
	xhr.open("GET","loadPowerTrend?IEEE="+escape(IEEE)+"&attribute="+escape(attribute)+"&time="+escape(time),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function loadDatasTrend(IEEE,attribute,time)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			
			leselect = xhr.responseText;
			document.getElementById("trend-datas").innerHTML=leselect;
		}
	}
	xhr.open("GET","loadDatasTrend?IEEE="+escape(IEEE)+"&attribute="+escape(attribute)+"&time="+escape(time),true);
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
			setTimeout(function(){ loadGaugeDashboard(div,IEEE,cluster,attribute,type,coefficient,min,max,label) }, 5000);
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

function loadDistributionChart(IEEE,time)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			var datas = JSON.parse(xhr.responseText);
			 donutChart.setData(datas);
		}
	}
	xhr.open("GET","loadDistributionChart?IEEE="+escape(IEEE)+"&time="+escape(time),true);
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
					pause = 6;
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
				}if (datas[0]==3)
				{
					document.getElementById('deviceFound').innerHTML='<svg id="icon" fill="#0f70b7" style="width:48px;" width="32" height="32" viewBox="0 0 24 24" role="img" xmlns="http://www.w3.org/2000/svg"><path d="M11.988 0a11.85 11.85 0 00-8.617 3.696c7.02-.875 11.401-.583 13.289-.34 3.752.583 3.558 3.404 3.558 3.404L8.237 19.112c2.299.22 6.897.366 13.796-.631a11.86 11.86 0 001.912-6.469C23.945 5.374 18.595 0 11.988 0zm.232 4.31c-2.451-.014-5.772.146-9.963.723C.854 7.003.055 9.41.055 12.012.055 18.626 5.38 24 11.988 24c3.63 0 6.85-1.63 9.053-4.182-7.286.948-11.813.631-13.75.388-3.775-.56-3.557-3.404-3.557-3.404L15.691 4.474a38.635 38.635 0 00-3.471-.163Z"></path></svg> '+datas[1];
					document.getElementById('nextBtn').style.display='block';
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

function setAlias(IEEE,alias)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			if (leselect =="OK")
			{
				window.location.href = '/configDevices';
			}
		}
	}
	xhr.open('GET','setAlias?ieee='+escape(IEEE)+'&alias='+escape(alias),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function getRuleStatus(name)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			const datas = leselect.split('|');
			if (datas[0] == "1")
			{
				var imsvg = "<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32' fill='#1bc600' class='bi bi-bookmark-check-fill' viewBox='0 0 16 16'> \
								<path fill-rule='evenodd' d='M2 15.5V2a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v13.5a.5.5 0 0 1-.74.439L8 13.069l-5.26 2.87A.5.5 0 0 1 2 15.5m8.854-9.646a.5.5 0 0 0-.708-.708L7.5 7.793 6.354 6.646a.5.5 0 1 0-.708.708l1.5 1.5a.5.5 0 0 0 .708 0z'/> \
							</svg>";
				
			}else{
				var imsvg = "<svg xmlns='http://www.w3.org/2000/svg' width='32' height='32' fill='#c60000' class='bi bi-bookmark-x-fill' viewBox='0 0 16 16'> \
								<path fill-rule='evenodd' d='M2 15.5V2a2 2 0 0 1 2-2h8a2 2 0 0 1 2 2v13.5a.5.5 0 0 1-.74.439L8 13.069l-5.26 2.87A.5.5 0 0 1 2 15.5M6.854 5.146a.5.5 0 1 0-.708.708L7.293 7 6.146 8.146a.5.5 0 1 0 .708.708L8 7.707l1.146 1.147a.5.5 0 1 0 .708-.708L8.707 7l1.147-1.146a.5.5 0 0 0-.708-.708L8 6.293z'/> \
							</svg>";
			}
			document.getElementById("status_"+name).innerHTML= imsvg;
			document.getElementById("dateStatus_"+name).innerHTML= datas[1];
			setTimeout(function(){ getRuleStatus(name); }, 5000);
		}
	}
	xhr.open('GET','getRuleStatus?id='+escape(name),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function getDeviceValue()
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
			if (leselect!="")
			{
				const res = eval(leselect);
				let len = res.length;
				
				for (let i=0;i <len; i++)
				{
					const datas = res[i].split(';');	
					var elem = document.getElementById(datas[0]);
					if(typeof elem !== 'undefined' && elem !== null) 
					{
						document.getElementById(datas[0]).innerHTML=datas[1];	
						document.getElementById(datas[0]).style.backgroundColor = "lightblue";
                        setTimeout(function(){ document.getElementById(datas[0]).style.backgroundColor ="transparent"; }, 5000);
					}
				}
			}
			setTimeout(function(){ getDeviceValue(); }, 5000);
		}
	}
	xhr.open('GET','getDeviceValue',true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function getLatestReleaseInfo() {
	$.getJSON("https://api.github.com/repos/fairecasoimeme/LiXee-Gateway/releases/latest").done(function(release) {
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
  