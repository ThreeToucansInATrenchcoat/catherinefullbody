#pragma once
namespace catherine {
inline unsigned resolve_tutorial_layout(unsigned active){return active<=4?active:1;}
// Active input IDs: keyboard=0, Xbox=1, PS5=2, Switch=3, PS4=4.
// Preference IDs follow the Config order: Auto, Xbox, Switch, PS4, PS5.
inline unsigned resolve_button_prompt_layout(unsigned active,int preference){
 if(!active)return 0;
 switch(preference){case 1:return 1;case 2:return 3;case 3:return 4;case 4:return 2;default:return active;}
}
}
