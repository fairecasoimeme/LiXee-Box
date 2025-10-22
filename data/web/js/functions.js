var powerGaugeAbo1;
var powerGaugeAbo2;
var powerGaugeAbo3;
var powerGaugeProd;
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

function ZigbeeAction(shortaddr,command,endpoint,value)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
		}
	}
	xhr.open("GET","ZigbeeAction?shortaddr="+escape(shortaddr)+"&command="+escape(command)+"&endpoint="+escape(endpoint)+"&value="+escape(value),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}

function ZigbeeSendRequest(shortaddr,endpoint,cluster,attribute)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
		}
	}
	xhr.open("GET","ZigbeeSendRequest?shortaddr="+escape(shortaddr)+"&endpoint="+escape(endpoint)+"&cluster="+escape(cluster)+"&attribute="+escape(attribute),true);
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
					unit=' VA';
					labelTime= Math.floor(datas[2]) + ' W';
				}else{
					unit=' Wh';
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
					textRenderer: function (val) {
						return val+unit;
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
								
			}else if (phase==2)
			{
				if (time=='hour')
				{
					unit=' VA';
				}else{
					unit=' Wh';
				}
				labelTime="";
				powerGaugeAbo2 = new JustGage({
					id: 'power_gauge_global2',
					value: datas[0],
					min: 0,
					max: datas[1],
					title: 'Target',
					label: labelTime,
					gaugeWidthScale: 0.6,
					pointer: true,
					textRenderer: function (val) {
						return val+unit;
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
			}else if (phase==3)
			{
				if (time=='hour')
				{
					unit=' VA';
				}else{
					unit=' Wh';
				}
				labelTime="";
				powerGaugeAbo3 = new JustGage({
					id: 'power_gauge_global3',
					value: datas[0],
					min: 0,
					max: datas[1],
					title: 'Target',
					label: labelTime,
					gaugeWidthScale: 0.6,
					pointer: true,
					textRenderer: function (val) {
						return val+unit;
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
				if (time=='hour')
				{
					unit=' VA';
					labelTime="";
					powerGaugeProd = new JustGage({
						id: 'power_gauge_prod',
						value: datas[0],
						min: 0,
						max: datas[1],
						title: 'Target',
						label: labelTime,
						gaugeWidthScale: 0.6,
						pointer: true,
						textRenderer: function (val) {
							return val+unit;
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
						customSectors: {
							percents: true,
							ranges: [{
								color : '#ff3b30',
								lo : 0,
								hi : 30
							},{
								color : '#f39c12',
								lo : 31,
								hi : 60
							},{
								color : '#43bf58',
								lo : 61,
								hi : 100
							}]
						},
						relativeGaugeSize: true,
						refreshAnimationTime: 1000
					});
				}else{
					unit=' Wh';
					powerGaugeProd = new JustGage({
						id: 'power_gauge_prod',
						value: datas[0],
						min: datas[1],
						max: 0,
						title: 'Target',
						label: "",
						gaugeWidthScale: 0.6,
						pointer: true,
						textRenderer: function (val) {
							return val+unit;
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
				}
				
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

function refreshStatusEnergy(IEEE,attribute,time,type)
{
	//refreshGaugeAbo(IEEE,attribute,time);
	loadPowerTrend(IEEE,attribute,time);
	loadDatasTrend(IEEE,attribute,time);
	loadLinkyDatas(IEEE);
	loadEnergyChart(IEEE,time,type);
	loadDistributionChart(time,"");
	if (time=='hour')
	{
		loadPowerChart(IEEE,attribute);
		setTimeout(function(){refreshStatusEnergy(IEEE,attribute,time,type); }, 15000);
	}else{
		setTimeout(function(){refreshStatusEnergy(IEEE,attribute,time,type); }, 60000);
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
			}else if (attribute == "519")
			{
				powerGaugeProd.refresh(datas[0],datas[1],datas[2],datas[3]);
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

function loadPowerChart(IEEE, attribute) {
    var xhr = getXhr();
    xhr.onreadystatechange = function() {
        if(xhr.readyState == 4) {
            var response = JSON.parse(xhr.responseText);
            var datas = response.datas;
            
            var labels = datas.map(function(d) { return d.y; });
            
            var inj1 = [], inj2 = [], inj3 = [];
            var pow1 = [], pow2 = [], pow3 = [];
            
            var hasInjection = false;
            
            datas.forEach(function(item) {
                var i1 = item['519'] || item['1'] || 0;
                var i2 = item['2'] || 0;
                var i3 = item['3'] || 0;
                
                inj1.push(i1 < 0 ? i1 : 0);
                inj2.push(i2 < 0 ? i2 : 0);
                inj3.push(i3 < 0 ? i3 : 0);
                
                if (i1 < 0 || i2 < 0 || i3 < 0) hasInjection = true;
                
                pow1.push((item['1295'] || 0) > 0 ? item['1295'] : 0);
                pow2.push((item['2319'] || 0) > 0 ? item['2319'] : 0);
                pow3.push((item['2575'] || 0) > 0 ? item['2575'] : 0);
            });
            
            powerChart.data.labels = labels;
            
            var isTriphasé = window.powerIsTriphasé || false;
            
            if (isTriphasé) {
                // TRIPHASÉ : 6 datasets (3 injections + 3 consommations)
                powerChart.data.datasets[0].data = inj1;
                powerChart.data.datasets[1].data = inj2;
                powerChart.data.datasets[2].data = inj3;
                powerChart.data.datasets[3].data = pow1;
                powerChart.data.datasets[4].data = pow2;
                powerChart.data.datasets[5].data = pow3;
                
                // Masquer les injections vides
                powerChart.data.datasets[0].hidden = !inj1.some(v => v < 0);
                powerChart.data.datasets[1].hidden = !inj2.some(v => v < 0);
                powerChart.data.datasets[2].hidden = !inj3.some(v => v < 0);
            } else {
                // MONOPHASÉ : 2 datasets (1 injection + 1 consommation)
                // Combiner toutes les injections dans le dataset 0
                var injTotal = inj1.map((v, i) => Math.min(0, v + inj2[i] + inj3[i]));
                powerChart.data.datasets[0].data = injTotal;
                powerChart.data.datasets[1].data = pow1;
                
                // Masquer l'injection si vide
                powerChart.data.datasets[0].hidden = !injTotal.some(v => v < 0);
            }
            
            // Calculer l'échelle dynamiquement EN INCLUANT LE GOAL
            var allValues = [...inj1, ...inj2, ...inj3, ...pow1, ...pow2, ...pow3];
            var minValue = Math.min(...allValues);
            var maxValue = Math.max(...allValues);
            
            // Inclure le goal dans le calcul si défini
            var goal = window.powerGoal || 0;
            if (goal > 0) {
                maxValue = Math.max(maxValue, goal);
            }
            
            // Calculer les limites avec marges
            var yMin = minValue < 0 ? Math.floor(minValue * 1.2) : 0;
            var yMax = Math.ceil(maxValue * 1.2);
            
            // S'assurer qu'on a au moins un peu d'espace
            if (yMax < 100) yMax = 100;
                      
            powerChart.options.scales.y.min = yMin;
            powerChart.options.scales.y.max = yMax;
            
            powerChart.update('active');
        }
    }
    xhr.open("GET", "loadPowerChart?IEEE=" + escape(IEEE) + "&attribute=" + escape(attribute), true);
    xhr.setRequestHeader('Content-Type', 'application/html');
    xhr.send();
}
/*function loadPowerChart(IEEE,attribute)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			var response = JSON.parse(xhr.responseText);
			var datas = response.datas;
			// Transformer les données en garantissant TOUTES les clés
            var transformedData = datas.map(function(item) {
                // Initialiser toutes les clés à 0
                var transformed = {
                    y: item.y,
                    '1': 0,
                    '2': 0,
                    '3': 0,
                    '1295': 0,
                    '2319': 0,
                    '2575': 0
                };
				
				var inj1 = item['1'] || 0;
                var inj2 = item['2'] || 0;
                var inj3 = item['3'] || 0;
                
                transformed['1'] = inj1 < 0 ? inj1 : 0;
                transformed['2'] = inj2 < 0 ? inj2 : 0;
                transformed['3'] = inj3 < 0 ? inj3 : 0;
                
                // Consommation (valeurs positives)
                transformed['1295'] = (item['1295'] || 0) > 0 ? item['1295'] : 0;
                transformed['2319'] = (item['2319'] || 0) > 0 ? item['2319'] : 0;
                transformed['2575'] = (item['2575'] || 0) > 0 ? item['2575'] : 0;

                
                return transformed;
			});
			// Debug : vérifier les données
            console.log('Échantillon avec injection:', transformedData.find(d => d['2'] < 0));
            console.log('Échantillon normal:', transformedData[0]);
            
			powerChart.setData(transformedData);	
		}
	}
	xhr.open("GET","loadPowerChart?IEEE="+escape(IEEE)+"&attribute="+escape(attribute),true);
	xhr.setRequestHeader('Content-Type','application/html');
	xhr.send();
}*/

/*function loadEnergyChart(IEEE,time)
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
}*/

function loadEnergyChart(IEEE, time,type) {
    // DEBUG - vérifier l'existence du chart       
    var xhr = getXhr();
    xhr.onreadystatechange = function() {
        if(xhr.readyState == 4) {
            if (xhr.status !== 200) {
                console.error('Erreur chargement:', xhr.status);
                return;
            }
            
            // LE SERVEUR RETOURNE DIRECTEMENT LE TABLEAU
            var datas = JSON.parse(xhr.responseText);
           
            var tarifInfo = window[type + 'TarifInfo'] || {};
            var keys = window[type + 'Keys'] || [];
            
            // Extraire les labels (heures/jours/mois)
            var labels = datas.map(function(d) { return d.y; });
            
            // Séparer les datasets en production (négatif) et consommation (positif)
            var datasetsProduction = [];
            var datasetsConsommation = [];
            var colors = getBarColors(type, keys.length);
            
            keys.forEach(function(key, index) {
                var keyStr = key.replace(/'/g, '');
                var info = tarifInfo[keyStr] || {};
                
                var dataProd = [];
                var dataConso = [];
                
                // Extraire et séparer les données
                datas.forEach(function(d) {
                    var value = d[keyStr] || 0;
                    
                    // Appliquer le coefficient si défini
                    if (info.coeff && info.coeff != 1) {
                        value = value * parseFloat(info.coeff);
                    }
                    
                    // Séparer production (négatif) et consommation (positif)
                    if (value < 0) {
                        dataProd.push(value);
                        dataConso.push(0);
                    } else {
                        dataProd.push(0);
                        dataConso.push(value);
                    }
                });
                
                // Créer dataset production si présent
                var hasProduction = dataProd.some(function(v) { return v < 0; });
                if (hasProduction) {
                    datasetsProduction.push({
                        label: (info.name || 'Section ' + keyStr) + ' (Prod)',
                        data: dataProd,
                        backgroundColor: '#27ae60', // Vert pour production
                        stack: 'Stack0'
                    });
                }
                
                // Créer dataset consommation si présent
                var hasConsommation = dataConso.some(function(v) { return v > 0; });
                if (hasConsommation) {
                    datasetsConsommation.push({
                        label: info.name || 'Section ' + keyStr,
                        data: dataConso,
                        backgroundColor: colors[index],
                        stack: 'Stack0'
                    });
                }
            });
            
            // Combiner : production d'abord, puis consommation
            var datasets = datasetsProduction.concat(datasetsConsommation);
            
            // Mettre à jour le graphique
            energyChart.data.labels = labels;
            energyChart.data.datasets = datasets;
            
            // Calculer l'échelle dynamiquement
            var allValues = [];
            datas.forEach(function(d) {
                keys.forEach(function(key) {
                    var keyStr = key.replace(/'/g, '');
                    var value = d[keyStr];
                    if (value !== undefined && value !== null) {
                        var info = tarifInfo[keyStr] || {};
                        if (info.coeff && info.coeff != 1) {
                            value = value * parseFloat(info.coeff);
                        }
                        allValues.push(value);
                    }
                });
            });
            
            var minValue = allValues.length > 0 ? Math.min(...allValues) : 0;
            var maxValue = allValues.length > 0 ? Math.max(...allValues) : 100;
            
            // Inclure le budget dans le calcul si défini
            var budget = window[type + 'Budget'] || 0;
            if (budget > 0) {
                maxValue = Math.max(maxValue, budget);
            }
            
            // Calculer les limites avec marges
            var yMin = minValue < 0 ? Math.floor(minValue * 1.2) : 0;
            var yMax = Math.ceil(maxValue * 1.2);
            
            if (yMax < 100) yMax = 100;
            
            energyChart.options.scales.y.min = yMin;
            energyChart.options.scales.y.max = yMax;
            
            energyChart.update('active');
        }
    }
    
    // L'URL doit correspondre à votre endpoint
    xhr.open("GET", "loadEnergyChart?IEEE=" + escape(IEEE) + "&time=" + escape(time), true);
    xhr.setRequestHeader('Content-Type', 'application/json');
    xhr.send();
}

// Fonction pour obtenir les couleurs selon le type
function getBarColors(type, count) {
    var baseColors = {
        'energy': ['#27ae60','#2980b9','#154360','#7f8c8d','#000000','#e74c3c','#c0392b'],
        'gaz': ['#e67e22'],
        'water': ['#3498db'],
        'production': ['#27ae60']
    };
    
    var colors = baseColors[type] || ['#1e88e5'];
    
    while (colors.length < count) {
        colors = colors.concat(baseColors[type]);
    }
    
    return colors.slice(0, count);
}

// Fonction pour le tooltip personnalisé
function getEnergyTooltipLabel(context, type) {
    var tarifInfo = window[type + 'TarifInfo'] || {};
    var keys = window[type + 'Keys'] || [];
    var value = context.parsed.y;
    
    // Extraire le label pour trouver la clé
    var datasetLabel = context.dataset.label || '';
    var isProduction = datasetLabel.includes('(Prod)');
    
    // Trouver la clé correspondante
    var keyStr = null;
    for (var i = 0; i < keys.length; i++) {
        var k = keys[i].replace(/'/g, '');
        var info = tarifInfo[k] || {};
        var labelToMatch = isProduction ? (info.name || 'Section ' + k) + ' (Prod)' : (info.name || 'Section ' + k);
        if (labelToMatch === datasetLabel) {
            keyStr = k;
            break;
        }
    }
    
    var info = tarifInfo[keyStr] || {};
    var unit = info.unit || 'Wh';
    
    var label = datasetLabel + ': ' + Math.abs(value).toFixed(0) + ' ' + unit;
    
    // Calculer le coût si prix défini
    if (info.price && info.price > 0) {
        var valueInKwh = unit === 'Wh' ? Math.abs(value) / 1000 : Math.abs(value);
        var cost = valueInKwh * parseFloat(info.price);
        
        if (info.taxe && info.taxe > 0) {
            cost += valueInKwh * parseFloat(info.taxe);
        }
        
        label += ' ≈ ' + cost.toFixed(3) + ' €';
    }
    
    return label;
}

// Fonction pour le footer du tooltip (total)
function getEnergyTooltipFooter(tooltipItems, type) {
    if (tooltipItems.length === 0) return '';
    
    var tarifInfo = window[type + 'TarifInfo'] || {};
    var keys = window[type + 'Keys'] || [];
    
    var totalProduction = 0;
    var totalConsommation = 0;
    var totalCostProd = 0;
    var totalCostConso = 0;
    var unit = 'Wh';
    
    tooltipItems.forEach(function(item) {
        var value = item.parsed.y;
        var datasetLabel = item.dataset.label || '';
        var isProduction = datasetLabel.includes('(Prod)');
        
        // Trouver la clé
        var keyStr = null;
        for (var i = 0; i < keys.length; i++) {
            var k = keys[i].replace(/'/g, '');
            var info = tarifInfo[k] || {};
            var labelToMatch = isProduction ? (info.name || 'Section ' + k) + ' (Prod)' : (info.name || 'Section ' + k);
            if (labelToMatch === datasetLabel) {
                keyStr = k;
                break;
            }
        }
        
        var info = tarifInfo[keyStr] || {};
        if (info.unit) unit = info.unit;
        
        if (value < 0) {
            totalProduction += Math.abs(value);
            if (info.price && info.price > 0) {
                var valueInKwh = unit === 'Wh' ? Math.abs(value) / 1000 : Math.abs(value);
                totalCostProd += valueInKwh * parseFloat(info.price);
            }
        } else {
            totalConsommation += value;
            if (info.price && info.price > 0) {
                var valueInKwh = unit === 'Wh' ? value / 1000 : value;
                var cost = valueInKwh * parseFloat(info.price);
                if (info.taxe && info.taxe > 0) {
                    cost += valueInKwh * parseFloat(info.taxe);
                }
                totalCostConso += cost;
            }
        }
    });
    
    var footer = '─────────────';
    
    if (totalProduction > 0) {
        footer += '\nProduction: ' + totalProduction.toFixed(0) + ' ' + unit;
        if (totalCostProd > 0) {
            footer += ' ≈ ' + totalCostProd.toFixed(3) + ' €';
        }
    }
    
    if (totalConsommation > 0) {
        footer += '\nConsommation: ' + totalConsommation.toFixed(0) + ' ' + unit;
        if (totalCostConso > 0) {
            footer += ' ≈ ' + totalCostConso.toFixed(3) + ' €';
        }
    }
    
    if (totalProduction > 0 && totalConsommation > 0) {
        var net = totalConsommation - totalProduction;
        var netCost = totalCostConso - totalCostProd;
        footer += '\nNet: ' + net.toFixed(0) + ' ' + unit;
        if (netCost != 0) {
            footer += ' ≈ ' + netCost.toFixed(3) + ' €';
        }
    }
    
    return footer;
}

function loadDistributionChart(time,type)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			var datas = JSON.parse(xhr.responseText);
			 donutChart.setData(datas);
		}
	}
	xhr.open("GET","loadDistributionChart?time="+escape(time)+"&type="+escape(type),true);
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
	var ico = document.getElementById('Ico'+div);
	if (x.style.display === "none") {
	  x.style.display = "block";
	  ico.innerHTML = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-dash-square' viewBox='0 0 16 16'> \
						<path d='M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2z'/> \
						<path d='M4 8a.5.5 0 0 1 .5-.5h7a.5.5 0 0 1 0 1h-7A.5.5 0 0 1 4 8'/> \
						</svg>";
	} else {
	  x.style.display = "none";
	  ico.innerHTML = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' fill='currentColor' class='bi bi-plus-square' viewBox='0 0 16 16'>\
                        <path d='M14 1a1 1 0 0 1 1 1v12a1 1 0 0 1-1 1H2a1 1 0 0 1-1-1V2a1 1 0 0 1 1-1zM2 0a2 2 0 0 0-2 2v12a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V2a2 2 0 0 0-2-2z'/>\
                        <path d='M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4'/>\
                      </svg>";
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
  