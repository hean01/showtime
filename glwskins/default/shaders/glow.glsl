
uniform float time;
uniform vec2 mouse;
uniform vec2 resolution;
uniform sampler2D u_tex;

varying vec4 f_tex;

const float COUNT = 5.0;

// MoltenMetal by CuriousChettai@gmail.com
//
// Modified by Henrik Andersson for Showtime.
//

void main( void ) {  
	vec2 uPos = ( gl_FragCoord.xy / resolution.y );//normalize wrt y axis
	uPos -= vec2((resolution.x/resolution.y)/2.0, 0.5);//shift origin to center

	float vertColor = 0.0;
	for(float i=0.0; i<COUNT; i++){
		float t = time*(i*0.1+1.)/24.0 + (i*0.1+0.1); 
		uPos.y += sin(-t+uPos.x*2.0)*0.45 -t*0.3;
		uPos.x += sin(-t+uPos.y*5.0)*0.25 ;
		float value = (sin(uPos.y*10.0*0.5)+sin(uPos.x*10.1+t*0.3) );
		
		float stripColor = 1.0/sqrt(abs(value));
		
		vertColor += stripColor/10.0;
	}
	
	float temp = min(max(0.1, vertColor), 1.0);
	vec3 texcol = texture2D(u_tex, f_tex.xy).xyz;
	
	// screen blend and multiply with tex
	temp = 1.0 - temp;
	texcol.r *= 1.0 - (temp * (1.0 - texcol.r));
	texcol.g *= 1.0 - (temp * (1.0 - texcol.g));
	texcol.b *= 1.0 - (temp * (1.0 - texcol.b));
	
	gl_FragColor = vec4(texcol, 1.0);
}
