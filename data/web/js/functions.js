var powerGaugeAbo1;
var powerGaugeAbo2;
var powerGaugeAbo3;
var powerGaugeProd;
var powerChart;
var energyChart;
var donutChart;

function formatEnergy(val) {
	var v = parseFloat(val);
	var absVal = Math.abs(v);
	if (absVal >= 1000000000) {
		return (v / 1000000000).toFixed(2) + ' GWh';
	} else if (absVal >= 1000000) {
		return (v / 1000000).toFixed(2) + ' MWh';
	} else if (absVal >= 1000) {
		return (v / 1000).toFixed(2) + ' kWh';
	} else {
		return Math.round(v) + ' Wh';
	}
}

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

function ZigbeeAction(shortaddr,command,endpoint,value,cluster, mfr)
{
	var xhr = getXhr();
	xhr.onreadystatechange = function(){
		if(xhr.readyState == 4 ){
			leselect = xhr.responseText;
		}
	}
	xhr.open("GET","ZigbeeAction?shortaddr="+escape(shortaddr)+"&command="+escape(command)+"&endpoint="+escape(endpoint)+"&value="+escape(value)+"&cluster="+escape(cluster)+"&mfr="+escape(mfr),true);
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
					labelTime='';
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
						if (time!='hour') return formatEnergy(val);
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
						if (time!='hour') return formatEnergy(val);
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
						if (time!='hour') return formatEnergy(val);
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
							return formatEnergy(val);
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
	// ✅ Réutiliser loadEnergyChart qui gère déjà Chart.js
	loadEnergyChart(IEEE, time, 'gaz');
}

function loadWaterChart(IEEE,time)
{
	// ✅ Réutiliser loadEnergyChart qui gère déjà Chart.js
	loadEnergyChart(IEEE, time, 'water');
}


/*function loadGazChart(IEEE,time)
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
}*/

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
	// Echelonnement des requetes (250 ms) pour ne pas saturer le tunnel : sans ca, ces 6 appels
	// partaient simultanement et remplissaient les 6 slots du tunnel (rejets 503 + deconnexions).
	var q=[
		function(){loadPowerTrend(IEEE,attribute,time);},
		function(){loadDatasTrend(IEEE,attribute,time);},
		function(){loadLinkyDatas(IEEE);},
		function(){loadEnergyChart(IEEE,time,type);},
		function(){loadDistributionChart(time,"");}
	];
	if (time=='hour') q.push(function(){loadPowerChart(IEEE,attribute);});
	var i=0;
	(function next(){
		if(i<q.length){ try{q[i]();}catch(e){} i++; setTimeout(next,250); }
		else { setTimeout(function(){refreshStatusEnergy(IEEE,attribute,time,type);}, time=='hour'?15000:60000); }
	})();
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
                var i1 = item['1'] || 0;
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

function loadEnergyChart(IEEE, time, type) {
	window[type + 'Period'] = time;
    
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
			if (type=="energy")
			{
				energyChart.data.labels = labels;
            	energyChart.data.datasets = datasets;
			}else if(type=="gaz")
			{
				gazChart.data.labels = labels;
            	gazChart.data.datasets = datasets;
			}else if(type=="water")
			{
				waterChart.data.labels = labels;
            	waterChart.data.datasets = datasets;
			}
            
            
            var minStacked = 0;  // Minimum des valeurs empilées (production)
            var maxStacked = 0;  // Maximum des valeurs empilées (consommation)
            
            // Pour chaque point sur l'axe X (chaque label)
            labels.forEach(function(label, index) {
                var sumPositive = 0;  // Somme des valeurs positives (consommation)
                var sumNegative = 0;  // Somme des valeurs négatives (production)
                
                // Parcourir tous les datasets
                datasets.forEach(function(dataset) {
                    var value = dataset.data[index] || 0;
                    
                    if (value > 0) {
                        sumPositive += value;
                    } else if (value < 0) {
                        sumNegative += value;
                    }
                });
                
                // Mettre à jour les min/max empilés
                if (sumPositive > maxStacked) {
                    maxStacked = sumPositive;
                }
                if (sumNegative < minStacked) {
                    minStacked = sumNegative;
                }
            });
            
            // Inclure le budget dans le calcul si défini
            var budget = window[type + 'Budget'] || 0;
            if (budget > 0) {
                maxStacked = Math.max(maxStacked, budget);
            }
            
            // Calculer la plage totale
            var range = maxStacked - minStacked;
            
            // Ajouter une marge de 10% en haut et en bas
            var margin = range > 0 ? range * 0.1 : 10;
            
            var yMin = minStacked < 0 ? Math.floor(minStacked - margin) : 0;
            var yMax = Math.ceil(maxStacked + margin);
            
            // Assurer un minimum de 100 pour l'échelle si trop petit
            if (yMax < 100) {
                yMax = 100;
            }
                        
           

			if (type=="energy")
			{
				energyChart.options.scales.y.min = yMin;
				energyChart.options.scales.y.max = yMax;
				energyChart.update('active');
				
			}else if(type=="gaz")
			{
            	gazChart.options.scales.y.min = yMin;
				gazChart.options.scales.y.max = yMax;
				gazChart.update('active');
			}else if(type=="water")
			{
            	waterChart.options.scales.y.min = yMin;
				waterChart.options.scales.y.max = yMax;
				waterChart.update('active');
			}
        }
    }
    
    xhr.open("GET", "loadEnergyChart?IEEE=" + escape(IEEE) + "&time=" + escape(time), true);
    xhr.setRequestHeader('Content-Type', 'application/json');
    xhr.send();
}

// Fonction pour obtenir les couleurs selon le type
function getBarColors(type, count) {
    var tarifInfo = window[type + 'TarifInfo'] || {};
    var keys = window[type + 'Keys'] || [];
    
    // Couleurs spécifiques par attribut pour l'énergie
    var energyAttrColors = {
        '0': '#2980b9',      // Total
        '256': '#2980b9',    // BASE/HC - bleu
        '258': '#154360',    // HP - bleu foncé
        '260': '#7f8c8d',    // HC Blanc - gris
        '262': '#000000',    // HP Blanc - noir
        '264': '#e74c3c',    // HC Rouge - rouge
        '266': '#c0392b',    // HP Rouge - rouge foncé
        '268': '#f5b041',    // EASF07 - orange
        '270': '#d35400',    // EASF08 - orange foncé
        '272': '#8e44ad',    // EASF09 - violet
        '274': '#6c3483',    // EASF10 - violet foncé
        '1': '#27ae60'       // Production - vert
    };
    
    var baseColors = {
        'gaz': ['#e67e22'],
        'water': ['#3498db'],
        'production': ['#27ae60']
    };
    
    var colors = [];
    
    // Parcourir les keys et attribuer la bonne couleur
    for (var i = 0; i < keys.length; i++) {
        var keyStr = keys[i].replace(/'/g, '');
        var info = tarifInfo[keyStr] || {};
        
        if (info.color) {
            // Couleur personnalisée (sous-compteurs)
            colors.push(info.color);
        } else if (type === 'energy' && energyAttrColors[keyStr]) {
            // Couleur spécifique pour cet attribut ZLinky
            colors.push(energyAttrColors[keyStr]);
        } else if (baseColors[type]) {
            // Couleur par défaut pour gaz/water/production
            colors.push(baseColors[type][0]);
        } else {
            // Fallback
            colors.push('#95a5a6');
        }
    }
    
    return colors;
}

// Fonction pour calculer les coûts détaillés
function calculateDetailedCost(kwh, info, period, isProduction) {
    var result = {
        energy: 0,
        subscription: 0,
        tax: 0,
        total: 0
    };
    
    if (info.price && info.price > 0) {
        result.energy = kwh * parseFloat(info.price);
    }
    
    if (!isProduction) {
        if (info.abo && info.abo > 0) {
            var abonnement = parseFloat(info.abo);
            
            switch(period) {
                case 'hour':
                    result.subscription = abonnement / (30 * 24);
                    break;
                case 'day':
                    result.subscription = abonnement / 30;
                    break;
                case 'month':
                    result.subscription = abonnement;
                    break;
                case 'year':
                    result.subscription = abonnement * 12;
                    break;
            }
        }
        
        if (info.taxe && info.taxe > 0 && kwh > 0) {
            result.tax = kwh * parseFloat(info.taxe);
        }
    }
    
    result.total = result.energy + result.subscription + result.tax;
    return result;
}

// Fonction pour le tooltip personnalisé
function getEnergyTooltipLabel(context, type) {
    var tarifInfo = window[type + 'TarifInfo'] || {};
    var keys = window[type + 'Keys'] || [];
    var period = window[type + 'Period'] || 'hour';
    var value = context.parsed.y;
    
    if (value === 0) return null;
    
    var datasetLabel = context.dataset.label || '';
    var isProduction = datasetLabel.includes('(Prod)');
    
    // Trouver la clé correspondante
    var keyStr = null;
    for (var i = 0; i < keys.length; i++) {
        var k = keys[i].replace(/'/g, '');
        var info = tarifInfo[k] || {};
        var labelToMatch = isProduction ? 
            (info.name || 'Section ' + k) + ' (Prod)' : 
            (info.name || 'Section ' + k);
        if (labelToMatch === datasetLabel) {
            keyStr = k;
            break;
        }
    }
    
    var info = tarifInfo[keyStr] || {};
    var unit = info.unit || 'Wh';
    var absValue = Math.abs(value);
    var valueInKwh = unit === 'Wh' ? absValue / 1000 : absValue;
    
    // Formater la ligne de base
    var label = datasetLabel + ': ' + absValue.toLocaleString('fr-FR') + ' ' + unit;
    
    // Calculer SEULEMENT énergie (+ taxes pour consommation)
    if (info.price && info.price > 0) {
        var cost = valueInKwh * parseFloat(info.price);
        
        // Ajouter les taxes uniquement pour la consommation
        if (!isProduction && info.taxe && info.taxe > 0) {
            cost += valueInKwh * parseFloat(info.taxe);
        }
        
        label += ' (' + cost.toFixed(2) + ' €)';
    }
    
    return label;
}

// Fonction pour le footer du tooltip (total)
function getEnergyTooltipFooter(tooltipItems, type) {
    if (tooltipItems.length === 0) return '';
    
    var tarifInfo = window[type + 'TarifInfo'] || {};
    var keys = window[type + 'Keys'] || [];
    var period = window[type + 'Period'] || 'hour';
    
    var totalProduction = 0;
    var totalConsommation = 0;
    
    // Coûts pour la production (seulement énergie)
    var prodEnergy = 0;
    
    // Coûts détaillés pour la consommation
    var consoEnergy = 0, consoTax = 0;
    
    var unit = 'Wh';
    
    // ← NOUVEAU : Variable pour stocker l'abonnement (une seule fois)
    var subscriptionAmount = 0;
    var subscriptionFound = false;
    
    // Parcourir tous les items du tooltip
    tooltipItems.forEach(function(item) {
        var value = item.parsed.y;
        if (value === 0) return;
        
        var datasetLabel = item.dataset.label || '';
        var isProduction = datasetLabel.includes('(Prod)');
        
        // Trouver la clé
        var keyStr = null;
        for (var i = 0; i < keys.length; i++) {
            var k = keys[i].replace(/'/g, '');
            var info = tarifInfo[k] || {};
            var labelToMatch = isProduction ? 
                (info.name || 'Section ' + k) + ' (Prod)' : 
                (info.name || 'Section ' + k);
            if (labelToMatch === datasetLabel) {
                keyStr = k;
                break;
            }
        }
        
        var info = tarifInfo[keyStr] || {};
        if (info.unit) unit = info.unit;
        
        var absValue = Math.abs(value);
        var valueInKwh = unit === 'Wh' ? absValue / 1000 : absValue;
        
        if (value < 0) {
            // Production - seulement énergie
            totalProduction += absValue;
            if (info.price && info.price > 0) {
                prodEnergy += valueInKwh * parseFloat(info.price);
            }
        } else {
            // Consommation
            totalConsommation += absValue;
            
            // Énergie
            if (info.price && info.price > 0) {
                consoEnergy += valueInKwh * parseFloat(info.price);
            }
            
            // Taxes (par kWh, donc additionnées)
            if (info.taxe && info.taxe > 0) {
                consoTax += valueInKwh * parseFloat(info.taxe);
            }

            // ← ABONNEMENT : compté UNE SEULE FOIS pour toute la période
            if (!subscriptionFound && info.abo && info.abo > 0) {
                var abonnement = parseFloat(info.abo);
                
                switch(period) {
                    case 'hour':
                        subscriptionAmount = abonnement / (30 * 24);
						if (info.taxe2 && info.taxe2 > 0) {
							consoTax += parseFloat(info.taxe2) / (30 * 24);
						}
                        break;
                    case 'day':
                        subscriptionAmount = abonnement / 30;
						if (info.taxe2 && info.taxe2 > 0) {
							consoTax += parseFloat(info.taxe2) / 30;
						}
                        break;
                    case 'month':
                        subscriptionAmount = abonnement;
						if (info.taxe2 && info.taxe2 > 0) {
							consoTax += parseFloat(info.taxe2);
						}
                        break;
                    case 'year':
                        subscriptionAmount = abonnement * 12;
						if (info.taxe2 && info.taxe2 > 0) {
							consoTax += parseFloat(info.taxe2) *12 ;
						}
                        break;
                }
                
                subscriptionFound = true; // Ne plus le compter
            }
        }
    });
    
    // Construire le footer
    var footer = '\n━━━━━━━━━━━━━━━━━━━━';
    
    // PRODUCTION (seulement revenu énergie)
    if (totalProduction > 0) {
        footer += '\n☀️ Production: ' + totalProduction.toLocaleString('fr-FR') + ' ' + unit;
        
        if (prodEnergy > 0) {
            footer += '\n   💵 Revenu: ' + prodEnergy.toFixed(2) + ' €';
            footer += '\n   • Énergie vendue: ' + prodEnergy.toFixed(2) + ' €';
        }
    }
    
    // CONSOMMATION (énergie + abonnement + taxes)
    if (totalConsommation > 0) {
        var totalConso = consoEnergy + subscriptionAmount + consoTax;
        
        footer += '\n⚡Consommation: ' + totalConsommation.toLocaleString('fr-FR') + ' ' + unit;
        
        if (totalConso > 0) {
            footer += '\n   💵 Coût: ' + totalConso.toFixed(2) + ' €';
            if (consoEnergy > 0) {
                footer += '\n   • Énergie: ' + consoEnergy.toFixed(2) + ' €';
            }
            if (subscriptionAmount > 0) {
                footer += '\n   • Abonnement: ' + subscriptionAmount.toFixed(2) + ' €';
            }
            if (consoTax > 0) {
                footer += '\n   • Taxes: ' + consoTax.toFixed(2) + ' €';
            }
        }
    }
    
    // NET (différence entre consommation et production)
    if (totalProduction > 0 && totalConsommation > 0) {
        var netEnergy = totalConsommation - totalProduction;
        var netCost = (consoEnergy + subscriptionAmount + consoTax) - prodEnergy;
        
        footer += '\n━━━━━━━━━━━━━━━━━━━━';
        footer += '\n📊 Net: ' + netEnergy.toLocaleString('fr-FR') + ' ' + unit;
        
        footer += '\n   💵 Facture nette: ' + netCost.toFixed(2) + ' €';
        footer += '\n   • Énergie nette: ' + (consoEnergy - prodEnergy).toFixed(2) + ' €';
        if (subscriptionAmount > 0) {
            footer += '\n   • Abonnement: ' + subscriptionAmount.toFixed(2) + ' €';
        }
        if (consoTax > 0) {
            footer += '\n   • Taxes: ' + consoTax.toFixed(2) + ' €';
        }
    } else if (totalProduction === 0 || totalConsommation === 0) {
        // Total simple
        var total = totalProduction + totalConsommation;
        
        if (totalProduction === 0) {
            // Que de la consommation
            var totalCostAll = consoEnergy + subscriptionAmount + consoTax;
            
            footer += '\n━━━━━━━━━━━━━━━━━━━━';
            footer += '\n⚡TOTAL: ' + total.toLocaleString('fr-FR') + ' ' + unit;
            footer += '\n  💵 Coût total: ' + totalCostAll.toFixed(2) + ' €';
            if (consoEnergy > 0) {
                footer += '\n   • Énergie: ' + consoEnergy.toFixed(2) + ' €';
            }
            if (subscriptionAmount > 0) {
                footer += '\n   • Abonnement: ' + subscriptionAmount.toFixed(2) + ' €';
            }
            if (consoTax > 0) {
                footer += '\n   • Taxes: ' + consoTax.toFixed(2) + ' €';
            }
        } else {
            // Que de la production
            footer += '\n━━━━━━━━━━━━━━━━━━━━';
            footer += '\n💵 TOTAL: ' + total.toLocaleString('fr-FR') + ' ' + unit;
            footer += '\n   Revenu total: ' + prodEnergy.toFixed(2) + ' €';
        }
    }
    
    return footer;
}


function loadDistributionChart(time, type)
{
    var xhr = getXhr();
    xhr.onreadystatechange = function() {
        if (xhr.readyState == 4) {
            var datas = JSON.parse(xhr.responseText);
            
            // Extraire les couleurs si présentes dans les données
            var colors = [];
            var hasCustomColors = false;
            
            for (var i = 0; i < datas.length; i++) {
                if (datas[i].color) {
                    colors.push(datas[i].color);
                    hasCustomColors = true;
                } else {
                    // Couleurs par défaut si pas de couleur spécifiée
                    var defaultColors = ['#2980b9', '#154360', '#7f8c8d', '#e74c3c', '#c0392b', '#f5b041', '#145a32', '#8e44ad'];
                    colors.push(defaultColors[i % defaultColors.length]);
                }
            }
            
            // Appliquer les couleurs personnalisées si présentes
            if (hasCustomColors && donutChart && donutChart.options) {
                donutChart.options.colors = colors;
            }
            
            // Mettre à jour les données
            donutChart.setData(datas);
        }
    }
    xhr.open("GET", "loadDistributionChart?time=" + escape(time) + "&type=" + escape(type), true);
    xhr.setRequestHeader('Content-Type', 'application/html');
    xhr.send();
}

// Fonction pour formater chaque ligne du tooltip
function getPowerTooltipLabel(context) {
    var value = context.parsed.y;
    
    // Si la valeur est 0, ne pas afficher
    if (value === 0) return null;
    
    var label = context.dataset.label || '';
    var absValue = Math.abs(value);
    
    // Formater : "Conso Ph1: 1 250 VA"
    return label + ': ' + absValue.toLocaleString('fr-FR') + ' VA';
}

// Fonction pour le footer avec totaux
function getPowerTooltipFooter(tooltipItems) {
    if (tooltipItems.length === 0) return '';
    
    var totalInjection = 0;
    var totalConsommation = 0;
    var isTriphasé = window.powerIsTriphasé || false;
    
    var injectionDetails = [];
    var consoDetails = [];
    
    // Parcourir tous les items du tooltip
    tooltipItems.forEach(function(item) {
        var value = item.parsed.y;
        if (value === 0) return; // Ignorer les valeurs nulles
        
        var label = item.dataset.label || '';
        var absValue = Math.abs(value);
        
        if (label.includes('Injection')) {
            totalInjection += absValue;
            if (isTriphasé) {
                injectionDetails.push({
                    phase: label.replace('Injection ', ''),
                    value: absValue
                });
            }
        } else {
            totalConsommation += absValue;
            if (isTriphasé) {
                consoDetails.push({
                    phase: label.replace('Conso ', ''),
                    value: absValue
                });
            }
        }
    });
    
    var footer = '\n━━━━━━━━━━━━━━━━━━━━';
    
    // Injection
    if (totalInjection > 0) {
        footer += '\n🟢 Injection: ' + totalInjection.toLocaleString('fr-FR') + ' VA';
        if (isTriphasé && injectionDetails.length > 0) {
            injectionDetails.forEach(function(detail) {
                footer += '\n   • ' + detail.phase + ': ' + detail.value.toLocaleString('fr-FR') + ' VA';
            });
        }
    }
    
    // Consommation
    if (totalConsommation > 0) {
        footer += '\n🔴 Consommation: ' + totalConsommation.toLocaleString('fr-FR') + ' VA';
        if (isTriphasé && consoDetails.length > 0) {
            consoDetails.forEach(function(detail) {
                footer += '\n   • ' + detail.phase + ': ' + detail.value.toLocaleString('fr-FR') + ' VA';
            });
        }
    }
    
    // Net (soutirée - injectée)
    if (totalInjection > 0 && totalConsommation > 0) {
        var net = totalConsommation - totalInjection;
        footer += '\n━━━━━━━━━━━━━━━━━━━━';
        footer += '\n📊 Puissance nette: ' + net.toLocaleString('fr-FR') + ' VA';
    } else if (totalInjection === 0 || totalConsommation === 0) {
        // Total simple
        var total = totalInjection + totalConsommation;
        footer += '\n━━━━━━━━━━━━━━━━━━━━';
        footer += '\n💡 TOTAL: ' + total.toLocaleString('fr-FR') + ' VA';
    }
    
    // Comparaison avec la limite (goal)
    var goal = window.powerGoal || 0;
    if (goal > 0 && totalConsommation > 0) {
        var percentOfGoal = (totalConsommation / goal * 100).toFixed(1);
        footer += '\n⚡ Utilisation: ' + percentOfGoal + '% de la limite';
        
        if (totalConsommation > goal) {
            var excess = totalConsommation - goal;
            footer += '\n⚠️  Dépassement: +' + excess.toLocaleString('fr-FR') + ' VA';
        } else {
            var remaining = goal - totalConsommation;
            footer += '\n✓ Marge: ' + remaining.toLocaleString('fr-FR') + ' VA';
        }
    }
    
    return footer;
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
					// Appareil trouve (appairage Zigbee ou LoRa). L'alerte peut tomber avant que
					// l'assistant n'affiche la zone, et getAlert() sert aussi des pages qui n'ont ni
					// zone ni bouton : on memorise sur window et on ne touche que ce qui existe.
					var found='<svg id="icon" fill="#0f70b7" style="width:48px;" width="32" height="32" viewBox="0 0 24 24" role="img" xmlns="http://www.w3.org/2000/svg"><path d="M11.988 0a11.85 11.85 0 00-8.617 3.696c7.02-.875 11.401-.583 13.289-.34 3.752.583 3.558 3.404 3.558 3.404L8.237 19.112c2.299.22 6.897.366 13.796-.631a11.86 11.86 0 001.912-6.469C23.945 5.374 18.595 0 11.988 0zm.232 4.31c-2.451-.014-5.772.146-9.963.723C.854 7.003.055 9.41.055 12.012.055 18.626 5.38 24 11.988 24c3.63 0 6.85-1.63 9.053-4.182-7.286.948-11.813.631-13.75.388-3.775-.56-3.557-3.404-3.557-3.404L15.691 4.474a38.635 38.635 0 00-3.471-.163Z"></path></svg> '+datas[1];
					window.deviceFound=true;
					window.deviceFoundInfo=found;
					var zone=document.getElementById('deviceFound');
					if (zone){zone.innerHTML=found;}
					var nBtn=document.getElementById('nextBtn');
					if (nBtn){nBtn.style.display='block';}
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

// setAlias() vivait ici mais n'avait qu'un seul appelant, l'assistant d'appairage, qui la
// definit desormais lui-meme (saveAlias) : cette page est servie par le firmware et sa
// navigation ne doit pas dependre de ce fichier, uploade separement.

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
  