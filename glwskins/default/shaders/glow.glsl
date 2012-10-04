
uniform float time;
uniform vec2 mouse;
uniform vec2 resolution;
uniform sampler2D tex;

const float COUNT = 3.0;

//MoltenMetal by CuriousChettai@gmail.com
//Linux fix

void main( void ) {  
	vec2 uPos = ( gl_FragCoord.xy / resolution.y );//normalize wrt y axis
	uPos -= vec2((resolution.x/resolution.y)/2.0, 0.5);//shift origin to center

	float vertColor = 0.0;
	for(float i=0.0; i<COUNT; i++){
		float t = time*(i*0.1+1.)/16.0 + (i*0.1+0.1); 
		uPos.y += sin(-t+uPos.x*2.0)*0.45 -t*0.3;
		uPos.x += sin(-t+uPos.y*5.0)*0.25 ;
		float value = (sin(uPos.y*10.0*0.5)+sin(uPos.x*10.1+t*0.3) );
		
		float stripColor = 1.0/sqrt(abs(value));
		
		vertColor += stripColor/10.0;
	}
	
	float temp = max(0.1, vertColor);
	vec3 texcol = texture2D(tex, gl_FragCoord.xy/resolution.xy).xyz;
	texcol.r = (texcol.r * 0.5) + (temp * 0.5);
	texcol.g = (texcol.g * 0.5) + (temp * 0.5);	
	texcol.b = (texcol.b * 0.5) + (temp * 0.5);


	gl_FragColor = vec4(texcol, 0.1);
}
